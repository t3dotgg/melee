#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

#include "platform/card.h"
#include <dolphin/card.h>
#include <sys/stat.h>
#include <sys/wait.h>

static DVDDiskID disc = { .gameName = { 'G', 'A', 'L', 'E' },
                          .company = { '0', '1' } };
static _Alignas(32) unsigned char work_area[CARD_WORKAREA_SIZE];
static _Alignas(32) unsigned char data[16384];
static _Alignas(32) unsigned char read_data[16384];
static int callbacks;
static int pending_count;
static int expected_channel;

DVDDiskID* DVDGetCurrentDiskID(void)
{
    return &disc;
}

static void callback(s32 chan, s32 result)
{
    CHECK(chan == expected_channel);
    CHECK(result == CARD_RESULT_READY);
    CHECK(pending_count == 1);
    CHECK(CARDGetResultCode(chan) == CARD_RESULT_READY);
    callbacks++;
    pending_count--;
}

static void finishAsync(void)
{
    int before = callbacks;
    CHECK(pending_count == 0);
    CHECK(CARDGetResultCode(expected_channel) == CARD_RESULT_BUSY);
    pending_count++;
    NativeCardPump();
    CHECK(pending_count == 0);
    CHECK(callbacks == before + 1);
    NativeCardPump();
    CHECK(callbacks == before + 1);
}

static void chainCallback(s32 chan, s32 result)
{
    callback(chan, result);
    CHECK(CARDCheckAsync(chan, callback) == CARD_RESULT_READY);
    pending_count++;
    int before = callbacks;
    NativeCardPump();
    CHECK(callbacks == before);
}

static void fillData(void)
{
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (unsigned char) (i * 37 + i / 256);
    }
}

static void testRestart(const char* root)
{
    CARDFileInfo file;
    CARDStat stat;
    CHECK(setenv("MELEE_SAVE_ROOT", root, 1) == 0);
    CARDInit();
    CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "melee-test", &file) == CARD_RESULT_READY);
    fillData();
    CHECK(CARDRead(&file, read_data, sizeof(read_data), 0) ==
          CARD_RESULT_READY);
    CHECK(memcmp(data, read_data, sizeof(data)) == 0);
    CHECK(CARDGetStatus(0, file.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(stat.iconAddr == 64 && stat.commentAddr == 0);
    CHECK(stat.offsetBanner == 64 && stat.offsetData == 7232);
    memset(data, 0xA5, 8192);
    CHECK(CARDWrite(&file, data, 8192, 8192) == CARD_RESULT_READY);
    CHECK(CARDClose(&file) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
}

static void runChild(const char* executable, const char* mode,
                     const char* root)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        execl(executable, executable, mode, root, (char*) NULL);
        _exit(127);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "--restart") == 0) {
        testRestart(argv[2]);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--locked") == 0) {
        CHECK(setenv("MELEE_SAVE_ROOT", argv[2], 1) == 0);
        CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_BUSY);
        return 0;
    }

    /* Every write stays under the caller's build directory. Each test run
     * creates its own root and never opens the default user save location. */
    const char* build = argc == 2 ? argv[1] : "build";
    CHECK(mkdir(build, 0700) == 0 || errno == EEXIST);
    char root[PATH_MAX];
    char image[PATH_MAX];
    char backup[PATH_MAX];
    char executable[PATH_MAX];
    CHECK(realpath(argv[0], executable) != NULL);
    CHECK(snprintf(root, sizeof(root), "%s/card-test.XXXXXX", build) > 0);
    CHECK(mkdtemp(root) != NULL);
    CHECK(setenv("MELEE_SAVE_ROOT", root, 1) == 0);
    CHECK(snprintf(image, sizeof(image), "%s/native-card-v1/slot-A.m4card",
                   root) > 0);
    CHECK(snprintf(backup, sizeof(backup), "%s/backup.m4card", root) > 0);

    s32 memory_size, sector_size, bytes, files;
    CARDFileInfo file, other;
    CARDStat stat;
    CARDInit();
    CHECK(setenv("MELEE_DISABLE_CARD", "1", 1) == 0);
    CHECK(CARDProbe(0) == 0);
    CHECK(CARDProbeEx(0, &memory_size, &sector_size) == CARD_RESULT_NOCARD);
    CHECK(memory_size == 0 && sector_size == 0);
    CHECK(CARDMountAsync(0, work_area, NULL, callback) == CARD_RESULT_NOCARD);
    NativeCardPump();
    CHECK(callbacks == 0 && access(image, F_OK) != 0);
    CHECK(CARDGetResultCode(0) == CARD_RESULT_NOCARD);
    CHECK(unsetenv("MELEE_DISABLE_CARD") == 0);
    CHECK(CARDProbe(-1) == 0 && CARDProbe(2) == 0);
    CHECK(CARDProbeEx(2, NULL, NULL) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDProbeEx(0, &memory_size, &sector_size) == CARD_RESULT_READY);
    CHECK(memory_size == 16 && sector_size == 8192);

    CHECK(CARDMountAsync(0, work_area, NULL, callback) == CARD_RESULT_READY);
    CHECK(callbacks == 0);
    CHECK(CARDOpen(0, "melee-test", &file) == CARD_RESULT_BUSY);
    finishAsync();
    CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_BUSY);
    runChild(executable, "--locked", root);
    CHECK(CARDCheckAsync(0, chainCallback) == CARD_RESULT_READY);
    pending_count++;
    NativeCardPump();
    CHECK(pending_count == 1 && CARDGetResultCode(0) == CARD_RESULT_BUSY);
    NativeCardPump();
    CHECK(pending_count == 0 && CARDGetResultCode(0) == CARD_RESULT_READY);
    CHECK(CARDFreeBlocks(0, &bytes, &files) == CARD_RESULT_READY);
    CHECK(bytes == 251 * 8192 && files == CARD_MAX_FILE);
    u16 encoding, memory;
    u32 sector;
    CHECK(CARDGetEncoding(0, &encoding) == CARD_RESULT_READY && encoding == 0);
    CHECK(CARDGetMemSize(0, &memory) == CARD_RESULT_READY && memory == 16);
    CHECK(CARDGetSectorSize(0, &sector) == CARD_RESULT_READY &&
          sector == 8192);

    CHECK(CARDOpen(0, "absent", &file) == CARD_RESULT_NOFILE &&
          file.chan == -1);
    CHECK(CARDFastOpen(0, 127, &file) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDCreate(0, "", 8192, &file) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDCreate(0, "melee-test", 8193, &file) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDCreate(0, "12345678901234567890123456789012345", 8192, &file) ==
          CARD_RESULT_NAMETOOLONG);
    CHECK(CARDCreate(0, "too-large", 252 * 8192, &file) ==
          CARD_RESULT_INSSPACE);
    CHECK(CARDCreateAsync(0, "melee-test", sizeof(data), &file, callback) ==
          CARD_RESULT_READY);
    int file_no = file.fileNo;
    finishAsync();
    CHECK(CARDCreateAsync(0, "melee-test", sizeof(data), &other, callback) ==
          CARD_RESULT_EXIST);
    int before = callbacks;
    NativeCardPump();
    CHECK(callbacks == before);
    CHECK(CARDFreeBlocks(0, &bytes, &files) == CARD_RESULT_READY);
    CHECK(bytes == 249 * 8192 && files == CARD_MAX_FILE - 1);
    CHECK(CARDGetStatus(0, file_no, &stat) == CARD_RESULT_READY);
    CHECK(stat.length == sizeof(data));
    CHECK(memcmp(stat.gameName, "GALE", 4) == 0 &&
          memcmp(stat.company, "01", 2) == 0);
    CHECK(stat.offsetBanner == (u32) -1 && stat.offsetData == 0);
    CHECK(CARDRead(&file, read_data, 512, 0) == CARD_RESULT_READY);
    for (int i = 0; i < 512; i++) {
        CHECK(read_data[i] == 0xFF);
    }

    fillData();
    CHECK(CARDWrite(&file, data + 1, 8192, 0) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDWrite(&file, data, 512, 0) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDWrite(&file, data, 8192, 512) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDRead(&file, read_data, 511, 0) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDRead(&file, read_data, 512, -512) == CARD_RESULT_FATAL_ERROR);
    CHECK(CARDRead(&file, read_data, 512, 16384) == CARD_RESULT_LIMIT);
    CHECK(CARDRead(&file, read_data, 8192, 12288) == CARD_RESULT_LIMIT);
    CHECK(CARDWriteAsync(&file, data, sizeof(data), 0, callback) ==
          CARD_RESULT_READY);
    CHECK(CARDRead(&file, read_data, 512, 0) == CARD_RESULT_BUSY);
    finishAsync();
    s32 transferred = CARDGetXferredBytes(0);
    CHECK(CARDReadAsync(&file, read_data, sizeof(read_data), 0, callback) ==
          CARD_RESULT_READY);
    finishAsync();
    CHECK(CARDGetXferredBytes(0) - transferred == sizeof(data));
    CHECK(memcmp(data, read_data, sizeof(data)) == 0);

    CHECK(CARDGetStatus(0, file_no, &stat) == CARD_RESULT_READY);
    stat.iconAddr = 64;
    stat.commentAddr = 0;
    stat.bannerFormat = CARD_STAT_BANNER_C8;
    stat.iconFormat = CARD_STAT_ICON_C8 | (CARD_STAT_ICON_RGB5A3 << 2);
    stat.iconSpeed = 1 | (2 << 2);
    /* SetStatus must ignore changes to identity and file length. */
    stat.length = 1;
    memcpy(stat.fileName, "changed", 7);
    CHECK(CARDSetStatusAsync(0, file_no, &stat, callback) ==
          CARD_RESULT_READY);
    finishAsync();
    CHECK(stat.offsetBanner == 64 && stat.offsetBannerTlut == 3136);
    CHECK(stat.offsetIcon[0] == 3648 && stat.offsetIcon[1] == 4672);
    CHECK(stat.offsetIconTlut == 6720 && stat.offsetData == 7232);
    CHECK(CARDGetStatus(0, file_no, &stat) == CARD_RESULT_READY);
    CHECK(stat.length == sizeof(data) &&
          strcmp(stat.fileName, "melee-test") == 0);
    stat.commentAddr = 8192 - 63;
    CHECK(CARDSetStatus(0, file_no, &stat) == CARD_RESULT_FATAL_ERROR);
    stat.commentAddr = 0;
    stat.iconAddr = 512;
    CHECK(CARDSetStatus(0, file_no, &stat) == CARD_RESULT_FATAL_ERROR);

    disc.gameName[3] = 'J';
    CHECK(CARDOpen(0, "melee-test", &other) == CARD_RESULT_NOFILE);
    CHECK(CARDGetStatus(0, file_no, &stat) == CARD_RESULT_READY);
    CHECK(CARDFastOpen(0, file_no, &other) == CARD_RESULT_READY);
    CHECK(CARDRead(&other, read_data, 512, 0) == CARD_RESULT_READY);
    CHECK(CARDWrite(&other, data, 8192, 0) == CARD_RESULT_NOPERM);
    CHECK(CARDSetStatus(0, file_no, &stat) == CARD_RESULT_NOPERM);
    CHECK(CARDFastDelete(0, file_no) == CARD_RESULT_NOPERM);
    disc.gameName[3] = 'E';
    CHECK(CARDClose(&file) == CARD_RESULT_READY && file.chan == -1);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    runChild(executable, "--restart", root);
    CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "melee-test", &file) == CARD_RESULT_READY &&
          file.fileNo == file_no);
    CHECK(CARDRead(&file, read_data, 8192, 8192) == CARD_RESULT_READY);
    for (int i = 0; i < 8192; i++) {
        CHECK(read_data[i] == 0xA5);
    }

    /* Failed persistence must keep the previous in-memory and disk bytes. */
    CHECK(rename(image, backup) == 0 && mkdir(image, 0700) == 0);
    CHECK(CARDWrite(&file, data, 8192, 8192) == CARD_RESULT_IOERROR);
    CHECK(CARDRead(&file, read_data, 8192, 8192) == CARD_RESULT_READY);
    for (int i = 0; i < 8192; i++) {
        CHECK(read_data[i] == 0xA5);
    }
    CHECK(rmdir(image) == 0 && rename(backup, image) == 0);

    CHECK(CARDCreate(0, "12345678901234567890123456789012", 8192, &other) ==
          CARD_RESULT_READY);
    CHECK(CARDRenameAsync(0, "12345678901234567890123456789012", "renamed",
                          callback) == CARD_RESULT_READY);
    finishAsync();
    CHECK(CARDRename(0, "renamed", "melee-test") == CARD_RESULT_EXIST);
    CHECK(CARDDeleteAsync(0, "renamed", callback) == CARD_RESULT_READY);
    finishAsync();
    CHECK(CARDOpen(0, "renamed", &other) == CARD_RESULT_NOFILE);
    CHECK(CARDFastOpen(0, file_no, &other) == CARD_RESULT_READY);
    CHECK(CARDCancel(&other) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    /* A bad checksum must not be treated as a new empty card. */
    FILE* corrupt = fopen(image, "r+b");
    CHECK(corrupt != NULL);
    CHECK(fseek(corrupt, -1, SEEK_END) == 0);
    int last = fgetc(corrupt);
    CHECK(last != EOF && fseek(corrupt, -1, SEEK_END) == 0);
    CHECK(fputc(last ^ 1, corrupt) != EOF && fclose(corrupt) == 0);
    CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_BROKEN);
    CHECK(CARDCheck(0) == CARD_RESULT_BROKEN);
    CHECK(CARDCreate(0, "should-fail", 8192, &file) == CARD_RESULT_BROKEN);
    CHECK(CARDFormatAsync(0, callback) == CARD_RESULT_READY);
    finishAsync();
    CHECK(CARDFreeBlocks(0, &bytes, &files) == CARD_RESULT_READY);
    CHECK(bytes == 251 * 8192 && files == 127);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    CHECK(CARDMount(0, work_area, NULL) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "melee-test", &file) == CARD_RESULT_NOFILE);
    CHECK(CARDCreate(0, "capacity", 251 * 8192, &file) == CARD_RESULT_READY);
    CHECK(CARDCreate(0, "full", 8192, &other) == CARD_RESULT_INSSPACE);
    CHECK(CARDFastDelete(0, file.fileNo) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    expected_channel = 1;
    CHECK(CARDMountAsync(1, work_area, NULL, callback) == CARD_RESULT_READY);
    finishAsync();
    CHECK(CARDCreate(1, "slot-b", 8192, &file) == CARD_RESULT_READY);
    CHECK(CARDUnmount(1) == CARD_RESULT_READY);
    puts("CARD persistence, restart, status, callbacks and failure cases "
         "passed");
    return 0;
}
