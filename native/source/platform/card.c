#include "card.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dolphin/card.h>
#include <sys/file.h>
#include <sys/stat.h>

/* This is a native save container, not a GameCube card image. Metadata uses
 * explicit big-endian fields. Files never contain host pointers or padding.
 * Each mutation replaces the complete container after fsync. A mount holds
 * an exclusive lock so another game process cannot overwrite its saves. */
enum {
    NATIVE_CARD_MBITS = 16,
    NATIVE_CARD_SECTOR = 8192,
    NATIVE_CARD_BLOCKS = 256,
    NATIVE_CARD_CAPACITY = (NATIVE_CARD_BLOCKS - 5) * NATIVE_CARD_SECTOR,
    NATIVE_CARD_HEADER = 24,
    NATIVE_CARD_DIRECTORY = CARD_MAX_FILE * 64,
};

typedef struct NativeCardFile {
    CARDStat stat;
    u8* data;
    u8 permission;
} NativeCardFile;

typedef struct NativeCard {
    NativeCardFile files[CARD_MAX_FILE];
    char path[PATH_MAX];
    int lock_fd;
    int directory_fd;
    BOOL mounted;
    BOOL broken;
    BOOL pending;
    s32 result;
    u32 transferred;
    CARDCallback callback;
} NativeCard;

static NativeCard cards[2];
static BOOL initialized;
static BOOL pumping;

static BOOL validChannel(s32 chan)
{
    return chan >= 0 && chan < 2;
}

static BOOL disabled(void)
{
    const char* value = getenv("MELEE_DISABLE_CARD");
    return value != NULL && strcmp(value, "0") != 0 && value[0] != '\0';
}

static u32 read32(const u8* data)
{
    return (u32) data[0] << 24 | (u32) data[1] << 16 | (u32) data[2] << 8 |
           data[3];
}

static void write32(u8* data, u32 value)
{
    data[0] = value >> 24;
    data[1] = value >> 16;
    data[2] = value >> 8;
    data[3] = value;
}

static u32 checksum(const u8* data, size_t length)
{
    u32 crc = UINT32_MAX;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xEDB88320u);
        }
    }
    return ~crc;
}

static u32 cardTime(void)
{
    time_t now = time(NULL);
    return now < 946684800 ? 0 : (u32) (now - 946684800);
}

static void freeFiles(NativeCardFile* files)
{
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        free(files[i].data);
        files[i] = (NativeCardFile){ 0 };
    }
}

void CARDInit(void)
{
    if (initialized) {
        return;
    }
    initialized = TRUE;
    for (int chan = 0; chan < 2; chan++) {
        cards[chan].lock_fd = -1;
        cards[chan].directory_fd = -1;
        cards[chan].result = CARD_RESULT_NOCARD;
    }
}

static s32 ready(s32 chan, BOOL allow_broken)
{
    CARDInit();
    if (!validChannel(chan)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (disabled() || !cards[chan].mounted) {
        return CARD_RESULT_NOCARD;
    }
    if (cards[chan].pending) {
        return CARD_RESULT_BUSY;
    }
    if (cards[chan].broken && !allow_broken) {
        return CARD_RESULT_BROKEN;
    }
    return CARD_RESULT_READY;
}

static s32 complete(s32 chan, s32 result, CARDCallback callback)
{
    cards[chan].result = result;
    if (result == CARD_RESULT_READY) {
        cards[chan].pending = TRUE;
        cards[chan].callback = callback;
    }
    return result;
}

void NativeCardPump(void)
{
    if (pumping) {
        return;
    }
    pumping = TRUE;
    for (int chan = 0; chan < 2; chan++) {
        NativeCard* card = &cards[chan];
        if (card->pending) {
            CARDCallback callback = card->callback;
            s32 result = card->result;
            card->pending = FALSE;
            card->callback = NULL;
            if (callback != NULL) {
                callback(chan, result);
            }
        }
    }
    pumping = FALSE;
}

static s32 syncResult(s32 chan, s32 result)
{
    if (result < 0) {
        return result;
    }
    /* Host I/O has finished. Sync APIs do not have a completion callback. */
    cards[chan].pending = FALSE;
    return cards[chan].result;
}

static BOOL makeDirectory(char* path)
{
    struct stat info;
    for (char* cursor = path + 1;; cursor++) {
        if (*cursor == '/' || *cursor == '\0') {
            char end = *cursor;
            *cursor = '\0';
            if ((mkdir(path, 0700) != 0 && errno != EEXIST) ||
                stat(path, &info) != 0 || !S_ISDIR(info.st_mode))
            {
                *cursor = end;
                return FALSE;
            }
            *cursor = end;
            if (end == '\0') {
                return TRUE;
            }
        }
    }
}

static s32 openContainer(s32 chan)
{
    NativeCard* card = &cards[chan];
    const char* root = getenv("MELEE_SAVE_ROOT");
    char directory[PATH_MAX];
    char lock_path[PATH_MAX];
    int length;
    if (root != NULL && root[0] != '\0') {
        length =
            snprintf(directory, sizeof(directory), "%s/native-card-v1", root);
    } else {
        const char* home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return CARD_RESULT_IOERROR;
        }
        length = snprintf(directory, sizeof(directory),
                          "%s/Library/Application Support/Melee4Mac Direct "
                          "ARM64/Saves/native-card-v1",
                          home);
    }
    if (length < 0 || (size_t) length >= sizeof(directory) ||
        !makeDirectory(directory))
    {
        return CARD_RESULT_IOERROR;
    }
    length = snprintf(card->path, sizeof(card->path), "%s/slot-%c.m4card",
                      directory, 'A' + chan);
    if (length < 0 || (size_t) length >= sizeof(card->path)) {
        return CARD_RESULT_IOERROR;
    }
    length = snprintf(lock_path, sizeof(lock_path), "%s.lock", card->path);
    if (length < 0 || (size_t) length >= sizeof(lock_path)) {
        return CARD_RESULT_IOERROR;
    }
    card->lock_fd =
        open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (card->lock_fd < 0) {
        return CARD_RESULT_IOERROR;
    }
    if (flock(card->lock_fd, LOCK_EX | LOCK_NB) != 0) {
        int error = errno;
        close(card->lock_fd);
        card->lock_fd = -1;
        return error == EWOULDBLOCK ? CARD_RESULT_BUSY : CARD_RESULT_IOERROR;
    }
    card->directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (card->directory_fd < 0) {
        close(card->lock_fd);
        card->lock_fd = -1;
        return CARD_RESULT_IOERROR;
    }
    return CARD_RESULT_READY;
}

static u32 usedBytes(const NativeCard* card)
{
    u32 size = 0;
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        if (card->files[i].data != NULL) {
            size += card->files[i].stat.length;
        }
    }
    return size;
}

static void packStat(u8* record, const CARDStat* stat)
{
    memcpy(record, stat->fileName, CARD_FILENAME_MAX);
    write32(record + 32, stat->length);
    write32(record + 36, stat->time);
    memcpy(record + 40, stat->gameName, 4);
    memcpy(record + 44, stat->company, 2);
    record[46] = stat->bannerFormat;
    write32(record + 48, stat->iconAddr);
    record[52] = stat->iconFormat >> 8;
    record[53] = stat->iconFormat;
    record[54] = stat->iconSpeed >> 8;
    record[55] = stat->iconSpeed;
    write32(record + 56, stat->commentAddr);
}

static void unpackStat(CARDStat* stat, const u8* record)
{
    memcpy(stat->fileName, record, CARD_FILENAME_MAX);
    stat->length = read32(record + 32);
    stat->time = read32(record + 36);
    memcpy(stat->gameName, record + 40, 4);
    memcpy(stat->company, record + 44, 2);
    stat->bannerFormat = record[46];
    stat->iconAddr = read32(record + 48);
    stat->iconFormat = (u16) record[52] << 8 | record[53];
    stat->iconSpeed = (u16) record[54] << 8 | record[55];
    stat->commentAddr = read32(record + 56);
}

static BOOL transfer(int fd, u8* data, size_t size, BOOL writing)
{
    while (size != 0) {
        ssize_t count = writing ? write(fd, data, size) : read(fd, data, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return FALSE;
        }
        data += count;
        size -= (size_t) count;
    }
    return TRUE;
}

static s32 persist(NativeCard* card)
{
    size_t size = NATIVE_CARD_HEADER + NATIVE_CARD_DIRECTORY + usedBytes(card);
    u8* image = calloc(1, size);
    char temporary[PATH_MAX];
    int fd;
    BOOL success;
    size_t cursor = NATIVE_CARD_HEADER + NATIVE_CARD_DIRECTORY;
    if (image == NULL) {
        return CARD_RESULT_IOERROR;
    }
    memcpy(image, "M4CARM64", 8);
    write32(image + 8, 1);
    write32(image + 12, (u32) size);
    write32(image + 20, NATIVE_CARD_CAPACITY);
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        NativeCardFile* file = &card->files[i];
        if (file->data != NULL) {
            packStat(image + NATIVE_CARD_HEADER + i * 64, &file->stat);
            image[NATIVE_CARD_HEADER + i * 64 + 47] = file->permission;
            memcpy(image + cursor, file->data, file->stat.length);
            cursor += file->stat.length;
        }
    }
    write32(image + 16,
            checksum(image + NATIVE_CARD_HEADER, size - NATIVE_CARD_HEADER));
    int length =
        snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", card->path);
    if (length < 0 || (size_t) length >= sizeof(temporary)) {
        free(image);
        return CARD_RESULT_IOERROR;
    }
    fd = mkstemp(temporary);
    if (fd < 0) {
        free(image);
        return CARD_RESULT_IOERROR;
    }
    success = transfer(fd, image, size, TRUE) && fsync(fd) == 0;
    if (close(fd) != 0) {
        success = FALSE;
    }
    if (success && rename(temporary, card->path) != 0) {
        success = FALSE;
    }
    if (success && fsync(card->directory_fd) != 0) {
        /* The rename has happened, but its durability is unknown. Block
         * further access until remount reloads the actual disk contents. */
        card->broken = TRUE;
        success = FALSE;
    }
    if (!success) {
        fprintf(stderr, "Native CARD could not save slot: %s\n",
                strerror(errno));
        unlink(temporary);
    }
    free(image);
    return success ? CARD_RESULT_READY : CARD_RESULT_IOERROR;
}

static BOOL validStat(CARDStat* stat)
{
    return stat->fileName[0] != '\0' && stat->length != 0 &&
           stat->length <= NATIVE_CARD_CAPACITY &&
           stat->length % NATIVE_CARD_SECTOR == 0 &&
           (stat->iconAddr == UINT32_MAX || stat->iconAddr < CARD_READ_SIZE) &&
           (stat->commentAddr == UINT32_MAX ||
            (stat->commentAddr <= stat->length - CARD_COMMENT_SIZE &&
             stat->commentAddr % NATIVE_CARD_SECTOR <=
                 NATIVE_CARD_SECTOR - CARD_COMMENT_SIZE));
}

static s32 loadContainer(NativeCard* card)
{
    struct stat info;
    u8* image;
    size_t cursor = NATIVE_CARD_HEADER + NATIVE_CARD_DIRECTORY;
    int fd = open(card->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return errno == ENOENT ? persist(card) : CARD_RESULT_IOERROR;
    }
    if (fstat(fd, &info) != 0) {
        close(fd);
        return CARD_RESULT_IOERROR;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < (off_t) cursor ||
        info.st_size > (off_t) (cursor + NATIVE_CARD_CAPACITY))
    {
        close(fd);
        return CARD_RESULT_BROKEN;
    }
    image = malloc((size_t) info.st_size);
    if (image == NULL) {
        close(fd);
        return CARD_RESULT_IOERROR;
    }
    BOOL success = transfer(fd, image, (size_t) info.st_size, FALSE);
    close(fd);
    if (!success) {
        free(image);
        return CARD_RESULT_IOERROR;
    }
    s32 result = CARD_RESULT_BROKEN;
    if (memcmp(image, "M4CARM64", 8) != 0 || read32(image + 8) != 1 ||
        read32(image + 12) != (u32) info.st_size ||
        read32(image + 20) != NATIVE_CARD_CAPACITY ||
        read32(image + 16) != checksum(image + NATIVE_CARD_HEADER,
                                       info.st_size - NATIVE_CARD_HEADER))
    {
        goto done;
    }
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        const u8* record = image + NATIVE_CARD_HEADER + i * 64;
        NativeCardFile* file = &card->files[i];
        unpackStat(&file->stat, record);
        file->permission = record[47];
        if (file->stat.length == 0) {
            const u8 empty[64] = { 0 };
            if (memcmp(record, empty, sizeof(empty)) != 0) {
                goto done;
            }
            continue;
        }
        if (!validStat(&file->stat) ||
            file->stat.length > (size_t) info.st_size - cursor)
        {
            goto done;
        }
        for (int j = 0; j < i; j++) {
            CARDStat* other = &card->files[j].stat;
            if (card->files[j].data != NULL &&
                strncmp(file->stat.fileName, other->fileName,
                        CARD_FILENAME_MAX) == 0 &&
                memcmp(file->stat.gameName, other->gameName, 4) == 0 &&
                memcmp(file->stat.company, other->company, 2) == 0)
            {
                goto done;
            }
        }
        file->data = malloc(file->stat.length);
        if (file->data == NULL) {
            result = CARD_RESULT_IOERROR;
            goto done;
        }
        memcpy(file->data, image + cursor, file->stat.length);
        cursor += file->stat.length;
    }
    if (cursor == (size_t) info.st_size) {
        result = CARD_RESULT_READY;
    }
done:
    free(image);
    if (result < 0) {
        freeFiles(card->files);
    }
    return result;
}

s32 CARDGetResultCode(s32 chan)
{
    CARDInit();
    if (!validChannel(chan)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (disabled() || !cards[chan].mounted) {
        return CARD_RESULT_NOCARD;
    }
    return cards[chan].pending ? CARD_RESULT_BUSY : cards[chan].result;
}

int CARDProbe(s32 chan)
{
    return validChannel(chan) && !disabled();
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    if (!validChannel(chan)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (memSize != NULL) {
        *memSize = disabled() ? 0 : NATIVE_CARD_MBITS;
    }
    if (sectorSize != NULL) {
        *sectorSize = disabled() ? 0 : NATIVE_CARD_SECTOR;
    }
    return disabled() ? CARD_RESULT_NOCARD : CARD_RESULT_READY;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback)
{
    (void) detachCallback;
    CARDInit();
    if (!validChannel(chan) || workArea == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (disabled()) {
        return CARD_RESULT_NOCARD;
    }
    NativeCard* card = &cards[chan];
    if (card->mounted) {
        return CARD_RESULT_BUSY;
    }
    s32 result = openContainer(chan);
    if (result < 0) {
        return result;
    }
    result = loadContainer(card);
    if (result == CARD_RESULT_READY || result == CARD_RESULT_BROKEN) {
        card->mounted = TRUE;
        card->broken = result == CARD_RESULT_BROKEN;
        if (card->broken) {
            fprintf(stderr,
                    "Native CARD slot %c has a corrupt save container. It was "
                    "not changed.\n",
                    'A' + chan);
        }
    } else {
        close(card->lock_fd);
        card->lock_fd = -1;
        close(card->directory_fd);
        card->directory_fd = -1;
    }
    return complete(chan, result, attachCallback);
}

s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback)
{
    return syncResult(chan,
                      CARDMountAsync(chan, workArea, detachCallback, NULL));
}

s32 CARDUnmount(s32 chan)
{
    CARDInit();
    if (!validChannel(chan)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    NativeCard* card = &cards[chan];
    if (card->pending) {
        return CARD_RESULT_BUSY;
    }
    if (card->lock_fd >= 0) {
        close(card->lock_fd);
    }
    if (card->directory_fd >= 0) {
        close(card->directory_fd);
    }
    freeFiles(card->files);
    *card = (NativeCard){ .lock_fd = -1,
                          .directory_fd = -1,
                          .result = CARD_RESULT_NOCARD };
    return CARD_RESULT_READY;
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback)
{
    s32 result = ready(chan, FALSE);
    return result < 0 ? result : complete(chan, CARD_RESULT_READY, callback);
}

s32 CARDCheck(s32 chan)
{
    return syncResult(chan, CARDCheckAsync(chan, NULL));
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)
{
    s32 result = ready(chan, FALSE);
    if (result < 0) {
        return result;
    }
    s32 free_files = 0;
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        free_files += cards[chan].files[i].data == NULL;
    }
    if (byteNotUsed != NULL) {
        *byteNotUsed = NATIVE_CARD_CAPACITY - usedBytes(&cards[chan]);
    }
    if (filesNotUsed != NULL) {
        *filesNotUsed = free_files;
    }
    return CARD_RESULT_READY;
}

static s32 validName(const char* name)
{
    if (name == NULL || name[0] == '\0') {
        return CARD_RESULT_FATAL_ERROR;
    }
    return strnlen(name, CARD_FILENAME_MAX + 1) > CARD_FILENAME_MAX
               ? CARD_RESULT_NAMETOOLONG
               : CARD_RESULT_READY;
}

static BOOL sameGame(const CARDStat* stat)
{
    const DVDDiskID* disc = DVDGetCurrentDiskID();
    return disc != NULL && memcmp(stat->gameName, disc->gameName, 4) == 0 &&
           memcmp(stat->company, disc->company, 2) == 0;
}

static s32 findFile(s32 chan, const char* name)
{
    s32 result = ready(chan, FALSE);
    if (result < 0) {
        return result;
    }
    result = validName(name);
    if (result < 0) {
        return result;
    }
    for (int i = 0; i < CARD_MAX_FILE; i++) {
        NativeCardFile* file = &cards[chan].files[i];
        if (file->data != NULL && sameGame(&file->stat) &&
            strncmp(name, file->stat.fileName, CARD_FILENAME_MAX) == 0)
        {
            return i;
        }
    }
    return CARD_RESULT_NOFILE;
}

static s32 getFile(s32 chan, s32 file_no, BOOL allow_public,
                   NativeCardFile** file)
{
    s32 result = ready(chan, FALSE);
    if (result < 0) {
        return result;
    }
    if (file_no < 0 || file_no >= CARD_MAX_FILE) {
        return CARD_RESULT_FATAL_ERROR;
    }
    *file = &cards[chan].files[file_no];
    if ((*file)->data == NULL) {
        return CARD_RESULT_NOFILE;
    }
    return sameGame(&(*file)->stat) ||
                   (allow_public && ((*file)->permission & CARD_ATTR_PUBLIC))
               ? CARD_RESULT_READY
               : CARD_RESULT_NOPERM;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo)
{
    NativeCardFile* file;
    if (fileInfo == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    fileInfo->chan = -1;
    s32 result = getFile(chan, fileNo, TRUE, &file);
    if (result < 0) {
        return result;
    }
    *fileInfo = (CARDFileInfo){ .chan = chan, .fileNo = fileNo, .iBlock = 5 };
    return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, char* fileName, CARDFileInfo* fileInfo)
{
    if (fileInfo == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    fileInfo->chan = -1;
    s32 file_no = findFile(chan, fileName);
    return file_no < 0 ? file_no : CARDFastOpen(chan, file_no, fileInfo);
}

s32 CARDClose(CARDFileInfo* fileInfo)
{
    if (fileInfo == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    s32 result = ready(fileInfo->chan, FALSE);
    if (result >= 0) {
        fileInfo->chan = -1;
    }
    return result;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size,
                    CARDFileInfo* fileInfo, CARDCallback callback)
{
    if (fileInfo == NULL || size == 0 || size % NATIVE_CARD_SECTOR != 0) {
        return CARD_RESULT_FATAL_ERROR;
    }
    fileInfo->chan = -1;
    s32 result = findFile(chan, fileName);
    if (result >= 0) {
        return CARD_RESULT_EXIST;
    }
    if (result != CARD_RESULT_NOFILE) {
        return result;
    }
    NativeCard* card = &cards[chan];
    if (size > NATIVE_CARD_CAPACITY - usedBytes(card)) {
        return CARD_RESULT_INSSPACE;
    }
    int slot;
    for (slot = 0; slot < CARD_MAX_FILE; slot++) {
        if (card->files[slot].data == NULL) {
            break;
        }
    }
    if (slot == CARD_MAX_FILE) {
        return CARD_RESULT_NOENT;
    }
    const DVDDiskID* disc = DVDGetCurrentDiskID();
    if (disc == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    NativeCardFile* file = &card->files[slot];
    file->data = malloc(size);
    if (file->data == NULL) {
        return CARD_RESULT_IOERROR;
    }
    memset(file->data, 0xFF, size);
    file->permission = CARD_ATTR_PUBLIC;
    file->stat = (CARDStat){ .length = size,
                             .time = cardTime(),
                             .iconAddr = UINT32_MAX,
                             .commentAddr = UINT32_MAX,
                             .iconSpeed = 1 };
    memcpy(file->stat.fileName, fileName,
           strnlen(fileName, CARD_FILENAME_MAX));
    memcpy(file->stat.gameName, disc->gameName, 4);
    memcpy(file->stat.company, disc->company, 2);
    result = persist(card);
    if (result < 0) {
        free(file->data);
        *file = (NativeCardFile){ 0 };
        return result;
    }
    *fileInfo = (CARDFileInfo){ .chan = chan, .fileNo = slot, .iBlock = 5 };
    return complete(chan, result, callback);
}

s32 CARDCreate(s32 chan, char* fileName, u32 size, CARDFileInfo* fileInfo)
{
    return syncResult(chan,
                      CARDCreateAsync(chan, fileName, size, fileInfo, NULL));
}

s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback)
{
    NativeCardFile* file;
    s32 result = getFile(chan, fileNo, FALSE, &file);
    if (result < 0) {
        return result;
    }
    NativeCardFile saved = *file;
    *file = (NativeCardFile){ 0 };
    result = persist(&cards[chan]);
    if (result < 0) {
        *file = saved;
    } else {
        free(saved.data);
    }
    return complete(chan, result, callback);
}

s32 CARDFastDelete(s32 chan, s32 fileNo)
{
    return syncResult(chan, CARDFastDeleteAsync(chan, fileNo, NULL));
}

s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback)
{
    s32 file_no = findFile(chan, fileName);
    return file_no < 0 ? file_no
                       : CARDFastDeleteAsync(chan, file_no, callback);
}

s32 CARDDelete(s32 chan, char* fileName)
{
    return syncResult(chan, CARDDeleteAsync(chan, fileName, NULL));
}

static s32 fileTransfer(CARDFileInfo* file_info, void* buffer, s32 length,
                        s32 offset, CARDCallback callback, BOOL writing)
{
    NativeCardFile* file;
    int alignment = writing ? NATIVE_CARD_SECTOR : CARD_READ_SIZE;
    if (file_info == NULL || buffer == NULL || (uintptr_t) buffer % 32 != 0 ||
        length <= 0 || offset < 0 || length % alignment != 0 ||
        offset % alignment != 0)
    {
        return CARD_RESULT_FATAL_ERROR;
    }
    s32 result = getFile(file_info->chan, file_info->fileNo, !writing, &file);
    if (result < 0) {
        return result;
    }
    if ((u32) offset >= file->stat.length ||
        (u32) length > file->stat.length - (u32) offset)
    {
        return CARD_RESULT_LIMIT;
    }
    NativeCard* card = &cards[file_info->chan];
    if (writing) {
        u8* data = malloc(file->stat.length);
        if (data == NULL) {
            return CARD_RESULT_IOERROR;
        }
        memcpy(data, file->data, file->stat.length);
        memcpy(data + offset, buffer, (size_t) length);
        u8* saved_data = file->data;
        u32 saved_time = file->stat.time;
        file->data = data;
        file->stat.time = cardTime();
        result = persist(card);
        if (result < 0) {
            file->data = saved_data;
            file->stat.time = saved_time;
            free(data);
            return result;
        }
        free(saved_data);
    } else {
        memcpy(buffer, file->data + offset, (size_t) length);
    }
    card->transferred += (u32) length;
    file_info->length = 0;
    file_info->offset =
        (offset + length - 1) / NATIVE_CARD_SECTOR * NATIVE_CARD_SECTOR;
    file_info->iBlock = 5 + file_info->offset / NATIVE_CARD_SECTOR;
    return complete(file_info->chan, CARD_RESULT_READY, callback);
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                  CARDCallback callback)
{
    return fileTransfer(fileInfo, buf, length, offset, callback, FALSE);
}

s32 CARDRead(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset)
{
    s32 result = CARDReadAsync(fileInfo, buf, length, offset, NULL);
    return result < 0 ? result : syncResult(fileInfo->chan, result);
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                   CARDCallback callback)
{
    return fileTransfer(fileInfo, buf, length, offset, callback, TRUE);
}

s32 CARDWrite(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset)
{
    s32 result = CARDWriteAsync(fileInfo, buf, length, offset, NULL);
    return result < 0 ? result : syncResult(fileInfo->chan, result);
}

s32 CARDCancel(CARDFileInfo* fileInfo)
{
    if (fileInfo == NULL || !validChannel(fileInfo->chan)) {
        return CARD_RESULT_FATAL_ERROR;
    }
    NativeCard* card = &cards[fileInfo->chan];
    if (!card->mounted || disabled()) {
        return CARD_RESULT_NOCARD;
    }
    /* Host transfers finish before returning. A pending completion cannot
     * undo bytes that are already on disk. */
    return CARD_RESULT_READY;
}

s32 CARDGetXferredBytes(s32 chan)
{
    return validChannel(chan) ? (s32) cards[chan].transferred : 0;
}

static void iconOffsets(CARDStat* stat)
{
    u32 offset = stat->iconAddr;
    BOOL palette = FALSE;
    if (offset == UINT32_MAX) {
        stat->bannerFormat = 0;
        stat->iconFormat = 0;
        stat->iconSpeed = 0;
        offset = 0;
    }
    stat->offsetBanner = UINT32_MAX;
    stat->offsetBannerTlut = UINT32_MAX;
    switch (CARDGetBannerFormat(stat)) {
    case CARD_STAT_BANNER_C8:
        stat->offsetBanner = offset;
        offset += CARD_BANNER_WIDTH * CARD_BANNER_HEIGHT;
        stat->offsetBannerTlut = offset;
        offset += 512;
        break;
    case CARD_STAT_BANNER_RGB5A3:
        stat->offsetBanner = offset;
        offset += 2 * CARD_BANNER_WIDTH * CARD_BANNER_HEIGHT;
        break;
    }
    for (int i = 0; i < CARD_ICON_MAX; i++) {
        stat->offsetIcon[i] = UINT32_MAX;
        switch (CARDGetIconFormat(stat, i)) {
        case CARD_STAT_ICON_C8:
            stat->offsetIcon[i] = offset;
            offset += CARD_ICON_WIDTH * CARD_ICON_HEIGHT;
            palette = TRUE;
            break;
        case CARD_STAT_ICON_RGB5A3:
            stat->offsetIcon[i] = offset;
            offset += 2 * CARD_ICON_WIDTH * CARD_ICON_HEIGHT;
            break;
        }
    }
    stat->offsetIconTlut = palette ? offset : UINT32_MAX;
    stat->offsetData = offset + (palette ? 512 : 0);
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    NativeCardFile* file;
    if (stat == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    s32 result = getFile(chan, fileNo, TRUE, &file);
    if (result >= 0) {
        *stat = file->stat;
        iconOffsets(stat);
    }
    return result;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat,
                       CARDCallback callback)
{
    NativeCardFile* file;
    if (stat == NULL) {
        return CARD_RESULT_FATAL_ERROR;
    }
    s32 result = getFile(chan, fileNo, FALSE, &file);
    if (result < 0) {
        return result;
    }
    CARDStat saved = file->stat;
    CARDStat changed = saved;
    changed.bannerFormat = stat->bannerFormat;
    changed.iconAddr = stat->iconAddr;
    changed.iconFormat = stat->iconFormat;
    changed.iconSpeed = stat->iconSpeed;
    changed.commentAddr = stat->commentAddr;
    changed.time = cardTime();
    iconOffsets(&changed);
    if (!validStat(&changed) || changed.offsetData > changed.length) {
        return CARD_RESULT_FATAL_ERROR;
    }
    file->stat = changed;
    result = persist(&cards[chan]);
    if (result < 0) {
        file->stat = saved;
    } else {
        /* The SDK changes only computed icon offsets in the caller's stat. */
        iconOffsets(stat);
    }
    return complete(chan, result, callback);
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    return syncResult(chan, CARDSetStatusAsync(chan, fileNo, stat, NULL));
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName,
                    CARDCallback callback)
{
    s32 file_no = findFile(chan, oldName);
    if (file_no < 0) {
        return file_no;
    }
    s32 result = findFile(chan, newName);
    if (result >= 0) {
        return CARD_RESULT_EXIST;
    }
    if (result != CARD_RESULT_NOFILE) {
        return result;
    }
    NativeCardFile* file = &cards[chan].files[file_no];
    CARDStat saved = file->stat;
    memset(file->stat.fileName, 0, CARD_FILENAME_MAX);
    memcpy(file->stat.fileName, newName, strnlen(newName, CARD_FILENAME_MAX));
    file->stat.time = cardTime();
    result = persist(&cards[chan]);
    if (result < 0) {
        file->stat = saved;
    }
    return complete(chan, result, callback);
}

s32 CARDRename(s32 chan, char* oldName, char* newName)
{
    return syncResult(chan, CARDRenameAsync(chan, oldName, newName, NULL));
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    s32 result = ready(chan, TRUE);
    if (result < 0) {
        return result;
    }
    NativeCard* card = &cards[chan];
    NativeCardFile saved[CARD_MAX_FILE];
    memcpy(saved, card->files, sizeof(saved));
    memset(card->files, 0, sizeof(card->files));
    result = persist(card);
    if (result < 0) {
        memcpy(card->files, saved, sizeof(saved));
    } else {
        freeFiles(saved);
        card->broken = FALSE;
    }
    return complete(chan, result, callback);
}

s32 CARDFormat(s32 chan)
{
    return syncResult(chan, CARDFormatAsync(chan, NULL));
}

s32 CARDGetEncoding(s32 chan, unsigned short* encode)
{
    s32 result = ready(chan, FALSE);
    if (result >= 0) {
        if (encode == NULL) {
            return CARD_RESULT_FATAL_ERROR;
        }
        *encode = 0;
    }
    return result;
}

s32 CARDGetMemSize(s32 chan, unsigned short* size)
{
    s32 result = ready(chan, FALSE);
    if (result >= 0) {
        if (size == NULL) {
            return CARD_RESULT_FATAL_ERROR;
        }
        *size = NATIVE_CARD_MBITS;
    }
    return result;
}

s32 CARDGetSectorSize(s32 chan, u32* size)
{
    s32 result = ready(chan, FALSE);
    if (result >= 0) {
        if (size == NULL) {
            return CARD_RESULT_FATAL_ERROR;
        }
        *size = NATIVE_CARD_SECTOR;
    }
    return result;
}
