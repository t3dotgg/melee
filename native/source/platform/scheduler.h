#ifndef MELEE_NATIVE_SCHEDULER_H
#define MELEE_NATIVE_SCHEDULER_H

#include <dolphin/os/OSAlarm.h>

/*
 * The native target has no VI interrupt. This scheduler supplies the one
 * timing source used by VI retraces and OS alarms. Production uses the host
 * monotonic clock. Tests can switch to a clock that advances only when asked.
 */
void NativeSchedulerInit(void);
void NativeSchedulerReset(void);
void NativeSchedulerSetDeterministic(BOOL enabled);
BOOL NativeSchedulerIsDeterministic(void);
OSTime NativeSchedulerGetTime(void);
void NativeSchedulerAdvance(OSTime ticks);
void NativeSchedulerSetFramePeriod(OSTime ticks);
OSTime NativeSchedulerGetFramePeriod(void);
void NativeSchedulerWaitForRetrace(void);
void NativeSchedulerPump(OSTime now);

void NativeSchedulerQueueAlarm(OSAlarm* alarm);
void NativeSchedulerRemoveAlarm(OSAlarm* alarm);
BOOL NativeSchedulerAlarmQueued(const OSAlarm* alarm);

#endif
