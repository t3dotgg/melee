#include <dolphin/dvd.h>

#ifdef MELEE_NATIVE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define NATIVE_DVD_MAX_ENTRIES 16384

typedef struct {
    char* path;
    u32 offset;
    u32 length;
    BOOL directory;
} NativeDVDEntry;

static NativeDVDEntry g_entries[NATIVE_DVD_MAX_ENTRIES];
static u32 g_entry_count;
static char g_root[PATH_MAX];
static int g_ready;
static int g_is_iso;
static int g_iso_fd = -1;
static char g_current_dir[PATH_MAX] = "/";
static DVDDiskID g_disk_id;

static u32 be32(const u8* p)
{
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

static void dvd_reset_entries(void)
{
    for (u32 i = 0; i < g_entry_count; i++) {
        free(g_entries[i].path);
    }
    g_entry_count = 0;
}

static int dvd_add_entry(const char* path, u32 offset, u32 length,
                         BOOL directory)
{
    if (g_entry_count >= NATIVE_DVD_MAX_ENTRIES) {
        return -1;
    }
    g_entries[g_entry_count].path = strdup(path);
    if (g_entries[g_entry_count].path == NULL) {
        return -1;
    }
    g_entries[g_entry_count].offset = offset;
    g_entries[g_entry_count].length = length;
    g_entries[g_entry_count].directory = directory;
    return (int) g_entry_count++;
}

static void dvd_normalize(const char* input, char* output, size_t size)
{
    size_t n = 0;
    while (*input != '\0' && n + 1 < size) {
        char c = *input++;
        if (c == '\\') {
            c = '/';
        }
        if (c == '/' && n > 0 && output[n - 1] == '/') {
            continue;
        }
        output[n++] = c;
    }
    output[n] = '\0';
    while (n > 0 && output[n - 1] == '/') {
        output[--n] = '\0';
    }
    if (output[0] == '\0') {
        strcpy(output, "/");
    } else if (output[0] != '/') {
        if (n + 1 < size) {
            memmove(output + 1, output, n + 1);
            output[0] = '/';
        }
    }
}

static const char* dvd_root_path(void)
{
    const char* root = getenv("MELEE_GAME_ROOT");
    if (root == NULL || root[0] == '\0') {
        root = getenv("MELEE_DISC_IMAGE");
    }
    return root != NULL && root[0] != '\0' ? root : ".";
}

static void dvd_scan_dir(const char* host, const char* game)
{
    DIR* dir = opendir(host);
    if (dir == NULL) {
        return;
    }
    struct dirent* item;
    while ((item = readdir(dir)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
        {
            continue;
        }
        char host_path[PATH_MAX];
        char game_path[PATH_MAX];
        if (snprintf(host_path, sizeof(host_path), "%s/%s", host,
                     item->d_name) >= (int) sizeof(host_path) ||
            snprintf(game_path, sizeof(game_path), "%s/%s", game,
                     item->d_name) >= (int) sizeof(game_path))
        {
            continue;
        }
        struct stat st;
        if (stat(host_path, &st) != 0) {
            continue;
        }
        BOOL directory = S_ISDIR(st.st_mode);
        dvd_add_entry(game_path, 0, directory ? 0 : (u32) st.st_size,
                      directory);
        if (directory) {
            dvd_scan_dir(host_path, game_path);
        }
    }
    closedir(dir);
}

static int dvd_load_iso(void)
{
    u8 header[0x430];
    if (pread(g_iso_fd, header, sizeof(header), 0) != (ssize_t) sizeof(header))
    {
        return 0;
    }
    memcpy(&g_disk_id, header, sizeof(g_disk_id));
    u32 fst_offset = be32(header + 0x424);
    u32 fst_size = be32(header + 0x428);
    if (fst_offset == 0 || fst_size < 12 || fst_size > 64 * 1024 * 1024) {
        return 0;
    }
    u8* fst = malloc(fst_size);
    if (fst == NULL || pread(g_iso_fd, fst, fst_size, (off_t) fst_offset) !=
                           (ssize_t) fst_size)
    {
        free(fst);
        return 0;
    }
    u32 count = be32(fst + 8);
    if (count == 0 || count > NATIVE_DVD_MAX_ENTRIES || count * 12 > fst_size)
    {
        free(fst);
        return 0;
    }
    const char* strings = (const char*) (fst + count * 12);
    size_t strings_size = fst_size - count * 12;
    char** paths = calloc(count, sizeof(*paths));
    if (paths == NULL) {
        free(fst);
        return 0;
    }
    paths[0] = strdup("/");
    dvd_add_entry("/", 0, 0, TRUE);
    for (u32 i = 1; i < count; i++) {
        u32 type_name = be32(fst + i * 12);
        u32 name_offset = type_name & 0x00ffffff;
        BOOL directory = (type_name & 0xff000000) != 0;
        u32 parent_or_offset = be32(fst + i * 12 + 4);
        u32 next_or_length = be32(fst + i * 12 + 8);
        if (name_offset >= strings_size) {
            continue;
        }
        const char* name = strings + name_offset;
        size_t name_len = strnlen(name, strings_size - name_offset);
        if (name_len == strings_size - name_offset) {
            continue;
        }
        u32 parent = 0;
        if (directory) {
            parent = parent_or_offset;
        } else {
            // File entries store their byte offset. Find the deepest directory
            // whose FST range contains this entry.
            u32 best = 0;
            for (u32 d = 0; d < i; d++) {
                u32 d_type = be32(fst + d * 12);
                u32 d_next = be32(fst + d * 12 + 8);
                if ((d_type & 0xff000000) != 0 && d_next > i && d >= best) {
                    best = d;
                }
            }
            parent = best;
        }
        const char* parent_path = paths[parent < i ? parent : 0];
        if (parent_path == NULL) {
            parent_path = "/";
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s%s%s", parent_path,
                 strcmp(parent_path, "/") == 0 ? "" : "/", name);
        paths[i] = strdup(path);
        dvd_add_entry(path, directory ? 0 : parent_or_offset,
                      directory ? 0 : next_or_length, directory);
    }
    for (u32 i = 0; i < count; i++) {
        free(paths[i]);
    }
    free(paths);
    free(fst);
    return 1;
}

static void dvd_init(void)
{
    if (g_ready) {
        return;
    }
    g_ready = 1;
    const char* root = dvd_root_path();
    snprintf(g_root, sizeof(g_root), "%s", root);
    struct stat st;
    if (stat(g_root, &st) == 0 && S_ISREG(st.st_mode)) {
        g_is_iso = 1;
        g_iso_fd = open(g_root, O_RDONLY);
        if (g_iso_fd >= 0 && dvd_load_iso()) {
            return;
        }
        if (g_iso_fd >= 0) {
            close(g_iso_fd);
        }
        g_iso_fd = -1;
        g_is_iso = 0;
    }
    /* Extracted game trees may include the original boot record. Only use
     * its identity when it is present. Never guess a disc revision. */
    {
        char boot_path[PATH_MAX];
        int fd;
        ssize_t got;
        if (snprintf(boot_path, sizeof(boot_path), "%s/sys/boot.bin", g_root) <
                (int) sizeof(boot_path) ||
            snprintf(boot_path, sizeof(boot_path), "%s/boot.bin", g_root) <
                (int) sizeof(boot_path))
        {
            fd = open(boot_path, O_RDONLY);
            if (fd >= 0) {
                got = read(fd, &g_disk_id, sizeof(g_disk_id));
                close(fd);
                if (got != (ssize_t) sizeof(g_disk_id)) {
                    memset(&g_disk_id, 0, sizeof(g_disk_id));
                }
            }
        }
    }
    dvd_add_entry("/", 0, 0, TRUE);
    dvd_scan_dir(g_root, "");
}

static NativeDVDEntry* dvd_entry(s32 entrynum)
{
    dvd_init();
    if (entrynum < 0 || (u32) entrynum >= g_entry_count) {
        return NULL;
    }
    return &g_entries[entrynum];
}

static NativeDVDEntry* dvd_file_for(const DVDFileInfo* info)
{
    dvd_init();
    if (info->cb.userData != NULL) {
        NativeDVDEntry* entry = (NativeDVDEntry*) info->cb.userData;
        if (entry >= g_entries && entry < g_entries + g_entry_count &&
            !entry->directory)
        {
            return entry;
        }
    }
    for (u32 i = 0; i < g_entry_count; i++) {
        if (!g_entries[i].directory &&
            g_entries[i].offset == info->startAddr &&
            g_entries[i].length == info->length)
        {
            return &g_entries[i];
        }
    }
    return NULL;
}

void DVDInit(void)
{
    dvd_init();
}

BOOL DVDCheckDisk(void)
{
    dvd_init();
    return g_entry_count > 1;
}

s32 DVDGetDriveStatus(void)
{
    return DVDCheckDisk() ? DVD_STATE_END : DVD_STATE_NO_DISK;
}

DVDDiskID* DVDGetCurrentDiskID(void)
{
    dvd_init();
    return &g_disk_id;
}

s32 DVDConvertPathToEntrynum(const char* path)
{
    dvd_init();
    if (path == NULL) {
        return -1;
    }
    char normalized[PATH_MAX];
    dvd_normalize(path, normalized, sizeof(normalized));
    for (u32 i = 0; i < g_entry_count; i++) {
        if (strcasecmp(g_entries[i].path, normalized) == 0) {
            return (s32) i;
        }
    }
    return -1;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* info)
{
    NativeDVDEntry* entry = dvd_entry(entrynum);
    if (entry == NULL || info == NULL || entry->directory) {
        return FALSE;
    }
    memset(info, 0, sizeof(*info));
    info->startAddr = entry->offset;
    info->length = entry->length;
    info->cb.userData = entry;
    info->cb.state = DVD_STATE_END;
    return TRUE;
}

BOOL DVDOpen(char* path, DVDFileInfo* info)
{
    s32 entry = DVDConvertPathToEntrynum(path);
    return entry < 0 ? FALSE : DVDFastOpen(entry, info);
}

BOOL DVDClose(DVDFileInfo* info)
{
    if (info == NULL) {
        return FALSE;
    }
    info->cb.state = DVD_STATE_END;
    info->callback = NULL;
    return TRUE;
}

static ssize_t dvd_read(const DVDFileInfo* info, void* dst, size_t length,
                        off_t offset)
{
    NativeDVDEntry* entry = dvd_file_for(info);
    if (entry == NULL || offset < 0 || (u64) offset > info->length) {
        return -1;
    }
    if (length > info->length - (u32) offset) {
        length = info->length - (u32) offset;
    }
    if (g_is_iso) {
        return pread(g_iso_fd, dst, length, (off_t) entry->offset + offset);
    }
    char host_path[PATH_MAX];
    snprintf(host_path, sizeof(host_path), "%s%s", g_root, entry->path);
    int fd = open(host_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t result = pread(fd, dst, length, offset);
    close(fd);
    return result;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* info, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio)
{
    (void) prio;
    if (info == NULL || addr == NULL || length < 0) {
        return FALSE;
    }
    info->cb.state = DVD_STATE_BUSY;
    ssize_t got = dvd_read(info, addr, (size_t) length, offset);
    info->cb.transferredSize = got < 0 ? 0 : (u32) got;
    info->cb.state = got < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
    if (callback != NULL) {
        callback(got < 0 ? DVD_RESULT_FATAL_ERROR : (s32) got, info);
    }
    return got >= 0;
}

s32 DVDReadPrio(DVDFileInfo* info, void* addr, s32 length, s32 offset,
                s32 prio)
{
    if (!DVDReadAsyncPrio(info, addr, length, offset, NULL, prio)) {
        return -1;
    }
    return (s32) info->cb.transferredSize;
}

int DVDSeekAsyncPrio(DVDFileInfo* info, s32 offset,
                     void (*callback)(s32, DVDFileInfo*), s32 prio)
{
    (void) prio;
    if (info == NULL || offset < 0 || (u32) offset > info->length) {
        return FALSE;
    }
    info->cb.offset = (u32) offset;
    info->cb.state = DVD_STATE_END;
    if (callback != NULL) {
        callback(DVD_RESULT_GOOD, info);
    }
    return TRUE;
}

s32 DVDSeekPrio(DVDFileInfo* info, s32 offset, s32 prio)
{
    return DVDSeekAsyncPrio(info, offset, NULL, prio) ? 0 : -1;
}

s32 DVDGetFileInfoStatus(DVDFileInfo* info)
{
    return info == NULL ? DVD_STATE_FATAL_ERROR : info->cb.state;
}

BOOL DVDGetCurrentDir(char* path, u32 maxlen)
{
    if (path == NULL || maxlen == 0) {
        return FALSE;
    }
    snprintf(path, maxlen, "%s", g_current_dir);
    return TRUE;
}

BOOL DVDChangeDir(char* path)
{
    if (path == NULL) {
        return FALSE;
    }
    char normalized[PATH_MAX];
    dvd_normalize(path, normalized, sizeof(normalized));
    s32 entry = DVDConvertPathToEntrynum(normalized);
    if (entry < 0 || !g_entries[entry].directory) {
        return FALSE;
    }
    snprintf(g_current_dir, sizeof(g_current_dir), "%s", normalized);
    return TRUE;
}

BOOL DVDPrepareStreamAsync(DVDFileInfo* info, u32 length, u32 offset,
                           DVDCallback callback)
{
    if (info == NULL || offset > info->length ||
        length > info->length - offset)
    {
        return FALSE;
    }
    info->cb.offset = offset;
    info->cb.state = DVD_STATE_END;
    if (callback != NULL) {
        callback(DVD_RESULT_GOOD, info);
    }
    return TRUE;
}
s32 DVDPrepareStream(DVDFileInfo* info, u32 length, u32 offset)
{
    return DVDPrepareStreamAsync(info, length, offset, NULL) ? 0 : -1;
}
s32 DVDGetTransferredSize(DVDFileInfo* info)
{
    return info == NULL ? 0 : (s32) info->cb.transferredSize;
}

#endif
