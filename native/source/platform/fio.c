#include <dolphin/mcc.h>

#ifdef MELEE_NATIVE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define NATIVE_FIO_HANDLES 64
static int g_fds[NATIVE_FIO_HANDLES];
static int g_initialized;
static u8 g_last_error;
static u32 g_async_result;
static int g_async_done = 1;

static const char* fio_root(void)
{
    const char* root = getenv("MELEE_GAME_ROOT");
    return root != NULL && root[0] != '\0' ? root : ".";
}

static int fio_path(const char* name, char* path, size_t size)
{
    if (name == NULL || path == NULL) {
        return 0;
    }
    while (*name == '/') {
        name++;
    }
    return snprintf(path, size, "%s/%s", fio_root(), name) < (int) size;
}

int FIOInit(enum MCC_EXI exiChannel, enum MCC_CHANNEL chID, u8 blockSize)
{
    (void) exiChannel;
    (void) chID;
    (void) blockSize;
    if (!g_initialized) {
        for (size_t i = 0; i < NATIVE_FIO_HANDLES; i++) {
            g_fds[i] = -1;
        }
        g_initialized = 1;
    }
    g_last_error = 0;
    return 1;
}
void FIOExit(void)
{
    for (size_t i = 0; i < NATIVE_FIO_HANDLES; i++) {
        if (g_fds[i] >= 0) {
            close(g_fds[i]);
        }
    }
    g_initialized = 0;
}
int FIOQuery(void)
{
    return g_initialized ? 1 : 0;
}
u8 FIOGetLastError(void)
{
    return g_last_error;
}

int FIOFopen(const char* filename, u32 mode)
{
    if (!g_initialized) {
        FIOInit(0, 0, 1);
    }
    char path[4096];
    if (!fio_path(filename, path, sizeof(path))) {
        g_last_error = EINVAL;
        return -1;
    }
    int flags = (mode & 0x2) ? O_RDWR | O_CREAT : O_RDONLY;
    if (mode & 0x4) {
        flags |= O_TRUNC;
    }
    int fd = open(path, flags, 0666);
    if (fd < 0) {
        g_last_error = (u8) errno;
        return -1;
    }
    for (int i = 0; i < NATIVE_FIO_HANDLES; i++) {
        if (g_fds[i] < 0) {
            g_fds[i] = fd;
            return i;
        }
    }
    close(fd);
    g_last_error = EMFILE;
    return -1;
}
int FIOFclose(int handle)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0) {
        return -1;
    }
    int result = close(g_fds[handle]);
    g_fds[handle] = -1;
    return result;
}
u32 FIOFread(int handle, void* data, u32 size)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0 ||
        data == NULL)
    {
        return 0;
    }
    ssize_t result = read(g_fds[handle], data, size);
    return result < 0 ? 0 : (u32) result;
}
u32 FIOFwrite(int handle, void* data, u32 size)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0 ||
        data == NULL)
    {
        return 0;
    }
    ssize_t result = write(g_fds[handle], data, size);
    return result < 0 ? 0 : (u32) result;
}
u32 FIOFseek(int handle, s32 offset, u32 mode)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0) {
        return (u32) -1;
    }
    int whence = mode == 0 ? SEEK_SET : mode == 1 ? SEEK_CUR : SEEK_END;
    off_t result = lseek(g_fds[handle], offset, whence);
    return result < 0 ? (u32) -1 : (u32) result;
}
int FIOFprintf(int handle, const char* format, ...)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0 ||
        format == NULL)
    {
        return -1;
    }
    char buffer[4096];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0) {
        return -1;
    }
    if ((size_t) length >= sizeof(buffer)) {
        length = (int) sizeof(buffer) - 1;
    }
    return (int) FIOFwrite(handle, buffer, (u32) length);
}
int FIOFflush(int handle)
{
    return (handle >= 0 && handle < NATIVE_FIO_HANDLES && g_fds[handle] >= 0)
               ? fsync(g_fds[handle])
               : -1;
}
int FIOFstat(int handle, struct FIO_Stat* stat_out)
{
    if (handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0 ||
        stat_out == NULL)
    {
        return -1;
    }
    struct stat st;
    if (fstat(g_fds[handle], &st) != 0) {
        return -1;
    }
    memset(stat_out, 0, sizeof(*stat_out));
    stat_out->fileSizeLow = (u32) st.st_size;
    return 0;
}
int FIOFerror(int handle)
{
    return handle < 0 || handle >= NATIVE_FIO_HANDLES || g_fds[handle] < 0;
}
int FIOFindFirst(const char* filename, struct FIO_Finddata* finddata)
{
    (void) filename;
    (void) finddata;
    return -1;
}
int FIOFindNext(struct FIO_Finddata* finddata)
{
    (void) finddata;
    return -1;
}
u32 FIOGetAsyncBufferSize(void)
{
    return 0;
}
int FIOFreadAsync(int handle, void* data, u32 size)
{
    g_async_result = FIOFread(handle, data, size);
    g_async_done = 1;
    return 1;
}
int FIOFwriteAsync(int handle, void* data, u32 size)
{
    g_async_result = FIOFwrite(handle, data, size);
    g_async_done = 1;
    return 1;
}
int FIOCheckAsyncDone(u32* result)
{
    if (!g_async_done) {
        return 0;
    }
    if (result != NULL) {
        *result = g_async_result;
    }
    return 1;
}

int MCCStreamOpen(enum MCC_CHANNEL chID, u8 blockSize)
{
    (void) chID;
    (void) blockSize;
    return 1;
}
int MCCStreamClose(enum MCC_CHANNEL chID)
{
    (void) chID;
    return 1;
}
int MCCStreamWrite(enum MCC_CHANNEL chID, void* data, u32 size)
{
    (void) chID;
    (void) data;
    return (int) size;
}
u32 MCCStreamRead(enum MCC_CHANNEL chID, void* data)
{
    (void) chID;
    (void) data;
    return 0;
}
int MCCInit(enum MCC_EXI exiChannel, u8 timeout, MCC_CBSysEvent callback)
{
    (void) exiChannel;
    (void) timeout;
    (void) callback;
    return 1;
}
void MCCExit(void) {}
int MCCPing(void)
{
    return 1;
}
int MCCEnumDevices(MCC_CBEnumDevices callback)
{
    if (callback != NULL) {
        callback(0);
    }
    return 1;
}
u8 MCCGetFreeBlocks(enum MCC_MODE mode)
{
    (void) mode;
    return 255;
}
u8 MCCGetLastError(void)
{
    return 0;
}
int MCCGetChannelInfo(enum MCC_CHANNEL chID, MCC_Info* info)
{
    (void) chID;
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }
    return 1;
}
int MCCGetConnectionStatus(enum MCC_CHANNEL chID, enum MCC_CONNECT* connect)
{
    (void) chID;
    if (connect != NULL) {
        *connect = MCC_CONNECT_CONNECTED;
    }
    return 1;
}
int MCCNotify(enum MCC_CHANNEL chID, u32 notify)
{
    (void) chID;
    (void) notify;
    return 1;
}
u32 MCCSetChannelEventMask(enum MCC_CHANNEL chID, u32 event)
{
    (void) chID;
    return event;
}
int MCCOpen(enum MCC_CHANNEL chID, u8 blockSize, MCC_CBEvent callback)
{
    (void) chID;
    (void) blockSize;
    (void) callback;
    return 1;
}
int MCCClose(enum MCC_CHANNEL chID)
{
    (void) chID;
    return 1;
}
int MCCLock(enum MCC_CHANNEL chID)
{
    (void) chID;
    return 1;
}
int MCCUnlock(enum MCC_CHANNEL chID)
{
    (void) chID;
    return 1;
}
int MCCRead(enum MCC_CHANNEL chID, u32 offset, void* data, s32 size,
            enum MCC_SYNC_STATE async)
{
    (void) chID;
    (void) offset;
    (void) data;
    (void) async;
    return size;
}
int MCCWrite(enum MCC_CHANNEL chID, u32 offset, void* data, s32 size,
             enum MCC_SYNC_STATE async)
{
    (void) chID;
    (void) offset;
    (void) data;
    (void) async;
    return size;
}
int MCCCheckAsyncDone(void)
{
    return 1;
}
#endif
