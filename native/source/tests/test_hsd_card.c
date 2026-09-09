#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/card.h"
#include <dolphin/card.h>
#include <dolphin/os.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/types.h>
#include <sys/wait.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/hsd_3B27.h>
#include <sysdolphin/baselib/memory.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            abort();                                                          \
        }                                                                     \
    } while (0)

static DVDDiskID disc = { .gameName = { 'G', 'A', 'L', 'E' },
                          .company = { '0', '1' } };
static BOOL interrupts_enabled = 1;
static int completed;
static int completion_result;
static void* allocated[2];
static int allocation_count;
static u8 icon_format[20] = { 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3 };
static u8 banner[0x1800];
static u8 icon[0x600];
static char description[64] = "Native HSD save test";
static _Alignas(32) u8 card_work[CARD_WORKAREA_SIZE];
static _Alignas(32) u8 sector[8192];
static int hsd_completions;
static char filename[32] = "native-hsd-test";
static u8 expected[5][0x5000];
static u8 payload[5][0x5000];
static struct CardEntry entries[6] = {
    { 11000, 3, payload[0] }, { 1000, 0, payload[1] },
    { 17000, 1, payload[2] }, { 10000, 2, payload[3] },
    { 12000, 3, payload[4] }, { -1, 0, NULL },
};

DVDDiskID* DVDGetCurrentDiskID(void)
{
    return &disc;
}

BOOL OSDisableInterrupts(void)
{
    BOOL previous = interrupts_enabled;
    interrupts_enabled = 0;
    return previous;
}

BOOL OSRestoreInterrupts(BOOL enabled)
{
    BOOL previous = interrupts_enabled;
    interrupts_enabled = enabled;
    if (enabled) {
        NativeCardPump();
    }
    return previous;
}

void* HSD_MemAlloc(ssize_t size)
{
    void* data;
    CHECK(size > 0 && allocation_count < 2);
    CHECK(posix_memalign(&data, 32, (size_t) size) == 0);
    allocated[allocation_count++] = data;
    return data;
}

void HSD_Free(void* data)
{
    free(data);
}

void __assert(char* file, u32 line, char* message)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, message);
    abort();
}

static void onComplete(int result)
{
    completion_result = result;
    completed++;
}

static int finish(int result)
{
    int polls = 0;
    while (result == 11 && polls++ < 10000) {
        result = lb_8001B6F8();
    }
    CHECK(polls < 10000);
    return result;
}

static void fillPayload(int version)
{
    for (size_t entry = 0; entry < 5; entry++) {
        for (int i = 0; i < entries[entry].file_size; i++) {
            payload[entry][i] = (u8) (i * 13 + i / 256 + entry * 7 + version);
        }
    }
    memcpy(expected, payload, sizeof(payload));
}

static void checkPayload(void)
{
    for (size_t entry = 0; entry < 5; entry++) {
        CHECK(memcmp(payload[entry], expected[entry],
                     entries[entry].file_size) == 0);
    }
}

static void onHsdComplete(s32 file, s32 result)
{
    CHECK(file == 0 || file == 1);
    CHECK(result == 0);
    hsd_completions++;
}

static void finishHsd(int count)
{
    for (int polls = 0; hsd_completions < count && polls < 10000; polls++) {
        hsd_803AAA48();
        NativeCardPump();
    }
    CHECK(hsd_completions == count);
}

static void testQueue(void)
{
    CardState state;
    CHECK(CARDMount(0, card_work, NULL) == 0);
    hsd_803B2374();
    hsd_803B24E4((void*) &state, 0, sizeof(sector), sector);
    hsd_completions = 0;
    CHECK(hsd_803B2550((void*) &state, filename, onHsdComplete) == 0);
    finishHsd(1);
    for (int batch = 0; batch < 2; batch++) {
        hsd_completions = 0;
        for (int request = 0; request < 32; request++) {
            CHECK(hsd_803B29D8((void*) &state, 1, payload[1], onHsdComplete) ==
                  0);
        }
        CHECK(hsd_completions == 0);
        CHECK(hsd_803B29D8((void*) &state, 1, payload[1], onHsdComplete) ==
              -265);
        finishHsd(32);
        CHECK(memcmp(payload[1], expected[1], entries[1].file_size) == 0);
    }
    CHECK(CARDUnmount(0) == 0);
}

static void runRestart(const char* executable, const char* root)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        execl(executable, executable, "read", root, (char*) NULL);
        _exit(127);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void cleanupScratch(const char* root)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/native-card-v1/slot-A.m4card", root);
    CHECK(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/native-card-v1/slot-A.m4card.lock", root);
    CHECK(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/native-card-v1", root);
    CHECK(rmdir(path) == 0);
    CHECK(rmdir(root) == 0);
}

int main(int argc, char** argv)
{
    char scratch[] = "/tmp/melee-native-hsd-card-XXXXXX";
    BOOL restart = argc == 3 && strcmp(argv[1], "read") == 0;
    char* root = restart ? argv[2] : mkdtemp(scratch);
    CHECK(root != NULL);
    CHECK(setenv("MELEE_SAVE_ROOT", root, 1) == 0);
    CHECK(unsetenv("MELEE_DISABLE_CARD") == 0);
    CARDInit();
    lb_8001C5BC();
    lbCardNew_AllocWorkArea();
    memset(banner, 0x42, sizeof(banner));
    memset(icon, 0x85, sizeof(icon));
    CHECK((uintptr_t) allocated[0] > UINT32_MAX);
    if (restart) {
        fillPayload(2);
        memset(payload, 0, sizeof(payload));
        CHECK(lb_8001BD34(0, filename, entries, NULL) == 0);
        checkPayload();
    } else {
        fillPayload(1);
        CHECK(finish(lb_8001BB48(0, filename, entries, icon_format,
                                 description, (HsdCardArg) banner,
                                 (HsdCardArg) icon, NULL)) == 0);
        memset(payload, 0, sizeof(payload));
        CHECK(lb_8001BD34(0, filename, entries, NULL) == 0);
        checkPayload();
        fillPayload(2);
        for (int attempt = 0; attempt < 2; attempt++) {
            completed = 0;
            int result = finish(lb_8001BE30(
                0, filename, entries, description, (HsdCardArg) banner,
                (HsdCardArg) icon, NULL, onComplete));
            CHECK(result == 0 && completed == 1 && completion_result == 0);
        }
        memset(payload, 0, sizeof(payload));
        CHECK(lb_8001BD34(0, filename, entries, NULL) == 0);
        checkPayload();
        runRestart(argv[0], root);
        memset(description, 0, sizeof(description));
        memset(banner, 0, sizeof(banner));
        memset(icon, 0, sizeof(icon));
        memset(payload, 0, sizeof(payload));
        CHECK(finish(lb_8001BF04(0, filename, entries, description,
                                 (HsdCardArg) banner, (HsdCardArg) icon,
                                 NULL)) == 0);
        CHECK(strcmp(description, "Native HSD save test") == 0);
        for (size_t i = 0; i < sizeof(banner); i++) {
            CHECK(banner[i] == 0x42);
        }
        for (size_t i = 0; i < sizeof(icon); i++) {
            CHECK(icon[i] == 0x85);
        }
        checkPayload();
        testQueue();

        /* Change card bytes through the SDK so the HSD checksum must catch it.
         */
        CARDFileInfo file;
        CHECK(CARDMount(0, card_work, NULL) == 0);
        CHECK(CARDOpen(0, filename, &file) == 0);
        CHECK(CARDRead(&file, sector, sizeof(sector), 0) == 0);
        sector[0x40] ^= 1;
        CHECK(CARDWrite(&file, sector, sizeof(sector), 0) == 0);
        CHECK(CARDClose(&file) == 0);
        CHECK(CARDUnmount(0) == 0);
        int corrupted = lb_8001BD34(0, filename, entries, NULL);
        CHECK(corrupted == 3);
        CHECK(lb_8001BA44(0, filename, NULL) == 0);
        CHECK(lb_8001BD34(0, filename, entries, NULL) == 4);
        cleanupScratch(root);
    }
    for (int i = 0; i < allocation_count; i++) {
        free(allocated[i]);
    }
    puts("HSD card create, save, restart, queue and corruption checks passed");
    return 0;
}
