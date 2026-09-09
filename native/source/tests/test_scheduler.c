#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "platform/scheduler.h"
#include <dolphin/card.h>
#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include <dolphin/vi.h>

static int failures;
static int alarm_calls;
static int retrace_calls;
static u32 callback_retrace;
static OSAlarm* callback_alarm;
static int card_callbacks;
static int card_pending;
static DVDDiskID disc = { .gameName = { 'G', 'A', 'L', 'E' },
                          .company = { '0', '1' } };
static _Alignas(32) u8 card_work[CARD_WORKAREA_SIZE];

DVDDiskID* DVDGetCurrentDiskID(void)
{
    return &disc;
}

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                    #condition);                                              \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void on_retrace(u32 count)
{
    retrace_calls++;
    callback_retrace = count;
}

static void on_alarm(OSAlarm* alarm, OSContext* context)
{
    (void) context;
    alarm_calls++;
    callback_alarm = alarm;
}

static void test_retrace_clock(void)
{
    NativeSchedulerReset();
    NativeSchedulerSetFramePeriod(OSMillisecondsToTicks(10));
    VIInit();
    VISetPreRetraceCallback(on_retrace);
    VISetPostRetraceCallback(NULL);
    CHECK(NativeSchedulerGetTime() == 0);
    VIWaitForRetrace();
    CHECK(VIGetRetraceCount() == 1);
    CHECK(retrace_calls == 1);
    CHECK(callback_retrace == 1);
    CHECK(NativeSchedulerGetTime() == OSMillisecondsToTicks(10));
}

static void test_one_shot_alarm(void)
{
    OSAlarm alarm;
    NativeSchedulerReset();
    alarm_calls = 0;
    callback_alarm = NULL;
    OSCreateAlarm(&alarm);
    OSSetAlarm(&alarm, OSMillisecondsToTicks(5), on_alarm);
    CHECK(OSCheckAlarmQueue());
    NativeSchedulerAdvance(OSMillisecondsToTicks(4));
    CHECK(alarm_calls == 0);
    NativeSchedulerAdvance(OSMillisecondsToTicks(1));
    CHECK(alarm_calls == 1);
    CHECK(callback_alarm == &alarm);
    CHECK(!OSCheckAlarmQueue());
    NativeSchedulerAdvance(OSMillisecondsToTicks(20));
    CHECK(alarm_calls == 1);
}

static void test_periodic_alarm_and_cancel(void)
{
    OSAlarm alarm;
    NativeSchedulerReset();
    alarm_calls = 0;
    OSCreateAlarm(&alarm);
    OSSetPeriodicAlarm(&alarm, OSMillisecondsToTicks(3),
                       OSMillisecondsToTicks(4), on_alarm);
    NativeSchedulerAdvance(OSMillisecondsToTicks(3));
    CHECK(alarm_calls == 1);
    NativeSchedulerAdvance(OSMillisecondsToTicks(4));
    CHECK(alarm_calls == 2);
    OSCancelAlarm(&alarm);
    CHECK(!OSCheckAlarmQueue());
    NativeSchedulerAdvance(OSMillisecondsToTicks(20));
    CHECK(alarm_calls == 2);
}

static void on_card(s32 chan, s32 result)
{
    CHECK(chan == 0);
    CHECK(result == CARD_RESULT_READY);
    CHECK(card_pending == 1);
    card_pending--;
    card_callbacks++;
}

static void test_card_interrupts(const char* build)
{
    char directory[4096];
    int length = snprintf(directory, sizeof(directory),
                          "%s/card-scheduler-XXXXXX", build);
    CHECK(length > 0 && (size_t) length < sizeof(directory));
    if (length <= 0 || (size_t) length >= sizeof(directory) ||
        mkdtemp(directory) == NULL)
    {
        failures++;
        return;
    }
    CHECK(setenv("MELEE_SAVE_ROOT", directory, 1) == 0);
    CHECK(unsetenv("MELEE_DISABLE_CARD") == 0);
    CARDInit();
    OSEnableInterrupts();
    BOOL level = OSDisableInterrupts();
    CHECK(CARDMountAsync(0, card_work, NULL, on_card) == CARD_RESULT_READY);
    CHECK(card_callbacks == 0);
    card_pending++;
    OSRestoreInterrupts(FALSE);
    CHECK(card_callbacks == 0);
    OSRestoreInterrupts(level);
    CHECK(card_callbacks == 1 && card_pending == 0);

    OSDisableInterrupts();
    CHECK(CARDCheckAsync(0, on_card) == CARD_RESULT_READY);
    card_pending++;
    CHECK(card_callbacks == 1);
    OSEnableInterrupts();
    CHECK(card_callbacks == 2 && card_pending == 0);

    CHECK(CARDCheckAsync(0, on_card) == CARD_RESULT_READY);
    card_pending++;
    CHECK(card_callbacks == 2);
    VIWaitForRetrace();
    CHECK(card_callbacks == 3 && card_pending == 0);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
}

int main(int argc, char** argv)
{
    test_retrace_clock();
    test_one_shot_alarm();
    test_periodic_alarm_and_cancel();
    test_card_interrupts(argc == 2 ? argv[1] : "build");
    return failures == 0 ? 0 : 1;
}
