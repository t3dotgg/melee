#include <assert.h>
#include <time.h>

static struct timespec clock_value;

static int test_clock_gettime(clockid_t clock_id, struct timespec* value)
{
    assert(clock_id == CLOCK_MONOTONIC);
    *value = clock_value;
    return 0;
}

#define clock_gettime test_clock_gettime
#include "../platform/scheduler.c"
#undef clock_gettime

u32 __OSBusClock = 162000000;

OSContext* OSGetCurrentContext(void)
{
    return NULL;
}

int main(void)
{
    /* The old nanosecond multiplication wrapped between these seconds. */
    clock_value.tv_sec = 455;
    NativeSchedulerInit();
    assert(NativeSchedulerGetTime() == 0);
    clock_value.tv_sec = 456;
    assert(NativeSchedulerGetTime() == OSSecondsToTicks(1));
    clock_value.tv_sec = 455 + 86400 * 30;
    clock_value.tv_nsec = 500000000;
    assert(NativeSchedulerGetTime() ==
           (OSTime) OSSecondsToTicks(1) * 86400 * 30 +
               OSMillisecondsToTicks(500));
    return 0;
}
