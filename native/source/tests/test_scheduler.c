#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include <dolphin/vi.h>

#include "platform/scheduler.h"

#include <stdio.h>

static int failures;
static int alarm_calls;
static int retrace_calls;
static u32 callback_retrace;
static OSAlarm* callback_alarm;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #condition);                                              \
            failures++;                                                        \
        }                                                                       \
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

int main(void)
{
    test_retrace_clock();
    test_one_shot_alarm();
    test_periodic_alarm_and_cancel();
    return failures == 0 ? 0 : 1;
}
