#include "scheduler.h"

#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include <dolphin/os.h>

typedef struct NativeSchedulerState {
    pthread_mutex_t lock;
    struct timespec clock_start;
    BOOL clock_started;
    BOOL initialized;
    BOOL deterministic;
    OSTime test_time;
    OSTime frame_period;
    OSTime next_retrace;
    OSAlarm* alarms;
} NativeSchedulerState;

static NativeSchedulerState s_scheduler = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .frame_period = 486000000 / 4 / 60,
};

static OSTime timespec_to_ticks(const struct timespec* value)
{
    uint64_t nanoseconds =
        (uint64_t) value->tv_sec * 1000000000u + (uint64_t) value->tv_nsec;
    return (OSTime) ((nanoseconds * (uint64_t) OS_TIMER_CLOCK) / 1000000000u);
}

static void ensure_clock_locked(void)
{
    if (!s_scheduler.clock_started) {
        clock_gettime(CLOCK_MONOTONIC, &s_scheduler.clock_start);
        s_scheduler.clock_started = TRUE;
    }
}

static OSTime monotonic_ticks_locked(void)
{
    struct timespec now;
    OSTime elapsed;

    ensure_clock_locked();
    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed =
        timespec_to_ticks(&now) - timespec_to_ticks(&s_scheduler.clock_start);
    return elapsed < 0 ? 0 : elapsed;
}

static void unlink_alarm_locked(OSAlarm* alarm)
{
    if (alarm->prev != NULL) {
        alarm->prev->next = alarm->next;
    } else if (s_scheduler.alarms == alarm) {
        s_scheduler.alarms = alarm->next;
    }
    if (alarm->next != NULL) {
        alarm->next->prev = alarm->prev;
    }
    alarm->prev = NULL;
    alarm->next = NULL;
}

void NativeSchedulerInit(void)
{
    pthread_mutex_lock(&s_scheduler.lock);
    if (!s_scheduler.initialized) {
        ensure_clock_locked();
        s_scheduler.initialized = TRUE;
        s_scheduler.next_retrace =
            monotonic_ticks_locked() + s_scheduler.frame_period;
    }
    pthread_mutex_unlock(&s_scheduler.lock);
}

void NativeSchedulerReset(void)
{
    OSAlarm* alarm;
    pthread_mutex_lock(&s_scheduler.lock);
    for (alarm = s_scheduler.alarms; alarm != NULL;) {
        OSAlarm* next = alarm->next;
        alarm->prev = NULL;
        alarm->next = NULL;
        alarm = next;
    }
    s_scheduler.alarms = NULL;
    s_scheduler.initialized = TRUE;
    s_scheduler.deterministic = TRUE;
    s_scheduler.test_time = 0;
    s_scheduler.frame_period = 486000000 / 4 / 60;
    s_scheduler.next_retrace = s_scheduler.frame_period;
    pthread_mutex_unlock(&s_scheduler.lock);
}

void NativeSchedulerSetDeterministic(BOOL enabled)
{
    pthread_mutex_lock(&s_scheduler.lock);
    ensure_clock_locked();
    s_scheduler.deterministic = enabled != FALSE;
    if (s_scheduler.deterministic) {
        s_scheduler.test_time = 0;
        s_scheduler.next_retrace = s_scheduler.frame_period;
    } else {
        s_scheduler.next_retrace =
            monotonic_ticks_locked() + s_scheduler.frame_period;
    }
    pthread_mutex_unlock(&s_scheduler.lock);
}

BOOL NativeSchedulerIsDeterministic(void)
{
    BOOL deterministic;
    pthread_mutex_lock(&s_scheduler.lock);
    deterministic = s_scheduler.deterministic;
    pthread_mutex_unlock(&s_scheduler.lock);
    return deterministic;
}

OSTime NativeSchedulerGetTime(void)
{
    OSTime time;
    pthread_mutex_lock(&s_scheduler.lock);
    ensure_clock_locked();
    time = s_scheduler.deterministic ? s_scheduler.test_time
                                     : monotonic_ticks_locked();
    pthread_mutex_unlock(&s_scheduler.lock);
    return time;
}

void NativeSchedulerAdvance(OSTime ticks)
{
    OSTime now;
    pthread_mutex_lock(&s_scheduler.lock);
    if (!s_scheduler.deterministic) {
        pthread_mutex_unlock(&s_scheduler.lock);
        return;
    }
    s_scheduler.test_time += ticks;
    now = s_scheduler.test_time;
    pthread_mutex_unlock(&s_scheduler.lock);
    NativeSchedulerPump(now);
}

void NativeSchedulerSetFramePeriod(OSTime ticks)
{
    if (ticks <= 0) {
        return;
    }
    pthread_mutex_lock(&s_scheduler.lock);
    s_scheduler.frame_period = ticks;
    s_scheduler.next_retrace =
        (s_scheduler.deterministic ? s_scheduler.test_time
                                   : monotonic_ticks_locked()) +
        ticks;
    pthread_mutex_unlock(&s_scheduler.lock);
}

OSTime NativeSchedulerGetFramePeriod(void)
{
    OSTime period;
    pthread_mutex_lock(&s_scheduler.lock);
    period = s_scheduler.frame_period;
    pthread_mutex_unlock(&s_scheduler.lock);
    return period;
}

void NativeSchedulerWaitForRetrace(void)
{
    OSTime target;
    OSTime now;

    pthread_mutex_lock(&s_scheduler.lock);
    ensure_clock_locked();
    if (s_scheduler.deterministic) {
        s_scheduler.test_time += s_scheduler.frame_period;
        pthread_mutex_unlock(&s_scheduler.lock);
        return;
    }
    target = s_scheduler.next_retrace;
    pthread_mutex_unlock(&s_scheduler.lock);

    for (;;) {
        struct timespec sleep_time;
        now = NativeSchedulerGetTime();
        if (now >= target) {
            break;
        }
        OSTime remaining = target - now;
        sleep_time.tv_sec = (time_t) (remaining / OS_TIMER_CLOCK);
        sleep_time.tv_nsec = (long) ((remaining % OS_TIMER_CLOCK) *
                                     1000000000 / OS_TIMER_CLOCK);
        nanosleep(&sleep_time, NULL);
    }

    pthread_mutex_lock(&s_scheduler.lock);
    now = monotonic_ticks_locked();
    do {
        s_scheduler.next_retrace += s_scheduler.frame_period;
    } while (s_scheduler.next_retrace <= now);
    pthread_mutex_unlock(&s_scheduler.lock);
}

void NativeSchedulerQueueAlarm(OSAlarm* alarm)
{
    pthread_mutex_lock(&s_scheduler.lock);
    alarm->prev = NULL;
    alarm->next = s_scheduler.alarms;
    if (s_scheduler.alarms != NULL) {
        s_scheduler.alarms->prev = alarm;
    }
    s_scheduler.alarms = alarm;
    pthread_mutex_unlock(&s_scheduler.lock);
}

void NativeSchedulerRemoveAlarm(OSAlarm* alarm)
{
    OSAlarm* current;
    if (alarm == NULL) {
        return;
    }
    pthread_mutex_lock(&s_scheduler.lock);
    for (current = s_scheduler.alarms; current != NULL;
         current = current->next)
    {
        if (current == alarm) {
            unlink_alarm_locked(alarm);
            break;
        }
    }
    pthread_mutex_unlock(&s_scheduler.lock);
}

BOOL NativeSchedulerAlarmQueued(const OSAlarm* alarm)
{
    const OSAlarm* current;
    BOOL found = FALSE;
    pthread_mutex_lock(&s_scheduler.lock);
    if (alarm == NULL) {
        found = s_scheduler.alarms != NULL;
    }
    for (current = s_scheduler.alarms; current != NULL;
         current = current->next)
    {
        if (alarm != NULL && current == alarm) {
            found = TRUE;
            break;
        }
    }
    pthread_mutex_unlock(&s_scheduler.lock);
    return found;
}

void NativeSchedulerPump(OSTime now)
{
    for (;;) {
        OSAlarm* alarm = NULL;
        OSAlarmHandler handler = NULL;
        OSTime period = 0;

        pthread_mutex_lock(&s_scheduler.lock);
        for (OSAlarm* current = s_scheduler.alarms; current != NULL;
             current = current->next)
        {
            if (current->handler != NULL && current->fire <= now) {
                alarm = current;
                break;
            }
        }
        if (alarm != NULL) {
            handler = alarm->handler;
            period = alarm->period;
            unlink_alarm_locked(alarm);
            if (period == 0) {
                alarm->handler = NULL;
            }
        }
        pthread_mutex_unlock(&s_scheduler.lock);

        if (alarm == NULL) {
            return;
        }
        handler(alarm, OSGetCurrentContext());

        if (period != 0 && alarm->handler == handler &&
            !NativeSchedulerAlarmQueued(alarm))
        {
            alarm->fire += period;
            NativeSchedulerQueueAlarm(alarm);
        }
    }
}
