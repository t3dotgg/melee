#include <dolphin/os.h>
#include <dolphin/os/OSAlarm.h>
#include <dolphin/os/OSThread.h>

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* GameCube's time base is one quarter of its 162 MHz bus clock. */
u32 __OSBusClock = 162000000u;
u32 __OSCoreClock = 486000000u;

static const size_t native_arena_size = 64u * 1024u * 1024u;
static void* native_arena;
static struct timespec native_time_start;
static int native_os_initialized;
static _Thread_local BOOL native_interrupts_enabled = TRUE;
static _Thread_local OSContext* native_current_context;
static _Thread_local OSThread* native_current_thread;
static u32 native_sound_mode = OS_SOUND_MODE_STEREO;
static u32 native_progressive_mode;
static u32 native_reset_code;

static OSTime monotonic_ticks(void)
{
    struct timespec now;
    uint64_t ns;
    uint64_t start_ns;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ns = (uint64_t) now.tv_sec * 1000000000u + (uint64_t) now.tv_nsec;
    start_ns = (uint64_t) native_time_start.tv_sec * 1000000000u +
               (uint64_t) native_time_start.tv_nsec;
    return (OSTime) ((ns - start_ns) * (uint64_t) OS_TIMER_CLOCK / 1000000000u);
}

void OSInit(void)
{
    if (native_os_initialized) {
        return;
    }
    native_os_initialized = 1;
    clock_gettime(CLOCK_MONOTONIC, &native_time_start);

    /* The native build has no boot ROM to provide an arena. */
    native_arena = aligned_alloc(32, native_arena_size);
    if (native_arena == NULL) {
        OSPanic(__FILE__, __LINE__, "Could not allocate native OS arena");
    }
    memset(native_arena, 0, native_arena_size);
    OSSetArenaLo(native_arena);
    OSSetArenaHi((u8*) native_arena + native_arena_size);
}

u32 OSGetConsoleType(void) { return OS_CONSOLE_PC_EMULATOR; }
u32 OSGetPhysicalMemSize(void) { return (u32) native_arena_size; }
u32 OSGetConsoleSimulatedMemSize(void) { return (u32) native_arena_size; }

OSTime OSGetTime(void)
{
    if (!native_os_initialized) {
        OSInit();
    }
    return monotonic_ticks();
}

OSTick OSGetTick(void) { return (OSTick) OSGetTime(); }

BOOL OSEnableInterrupts(void)
{
    BOOL old = native_interrupts_enabled;
    native_interrupts_enabled = TRUE;
    return old;
}

BOOL OSDisableInterrupts(void)
{
    BOOL old = native_interrupts_enabled;
    native_interrupts_enabled = FALSE;
    return old;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    BOOL old = native_interrupts_enabled;
    native_interrupts_enabled = level != FALSE;
    return old;
}

void OSInitContext(OSContext* context, uptr pc, uptr newsp)
{
    memset(context, 0, sizeof(*context));
    context->srr0 = (u32) pc;
    context->gpr[1] = (u32) newsp;
    context->state = OS_CONTEXT_STATE_FPSAVED;
}

void OSClearContext(OSContext* context) { memset(context, 0, sizeof(*context)); }
OSContext* OSGetCurrentContext(void) { return native_current_context; }
void OSSetCurrentContext(OSContext* context) { native_current_context = context; }
void OSLoadContext(OSContext* context) { native_current_context = context; }
u32 OSSaveContext(OSContext* context)
{
    if (context != NULL) {
        context->state = OS_CONTEXT_STATE_FPSAVED;
    }
    return 0;
}
void OSLoadFPUContext(OSContext* context) { (void) context; }
void OSSaveFPUContext(OSContext* context) { (void) context; }
void OSFillFPUContext(OSContext* context) { (void) context; }
uptr OSGetStackPointer(void)
{
    return (uptr) __builtin_frame_address(0);
}
uptr OSSwitchStack(uptr newsp) { return newsp; }
int OSSwitchFiber(uptr pc, uptr newsp)
{
    (void) pc;
    (void) newsp;
    return 0;
}

static int native_scheduler_disabled;
static pthread_mutex_t native_thread_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct NativeThreadSlot {
    OSThread* thread;
    void* (*entry)(void*);
    void* argument;
    pthread_t handle;
    int started;
} NativeThreadSlot;

static NativeThreadSlot native_threads[128];

static NativeThreadSlot* find_thread(OSThread* thread)
{
    size_t i;
    for (i = 0; i < sizeof(native_threads) / sizeof(native_threads[0]); i++) {
        if (native_threads[i].thread == thread) {
            return &native_threads[i];
        }
    }
    return NULL;
}

static void* native_thread_main(void* argument)
{
    NativeThreadSlot* slot = argument;
    native_current_thread = slot->thread;
    /* The caller can allocate OSThread on its stack. Do not touch it after
     * the entry point returns, because that stack may already be gone. */
    return slot->entry(slot->argument);
}

void OSInitThreadQueue(OSThreadQueue* queue)
{
    queue->head = NULL;
    queue->tail = NULL;
}
void OSSleepThread(OSThreadQueue* queue) { (void) queue; }
void OSWakeupThread(OSThreadQueue* queue) { (void) queue; }
s32 OSSuspendThread(OSThread* thread)
{
    s32 old = thread == NULL ? 0 : thread->suspend;
    if (thread != NULL) {
        thread->suspend++;
        thread->state = OS_THREAD_STATE_WAITING;
    }
    return old;
}
s32 OSResumeThread(OSThread* thread)
{
    NativeThreadSlot* slot;
    s32 old;
    if (thread == NULL) {
        return -1;
    }
    old = thread->suspend;
    thread->suspend = 0;
    thread->state = OS_THREAD_STATE_RUNNING;
    pthread_mutex_lock(&native_thread_lock);
    slot = find_thread(thread);
    if (slot != NULL && !slot->started) {
        slot->started = pthread_create(&slot->handle, NULL, native_thread_main, slot) == 0;
        if (!slot->started) {
            thread->state = OS_THREAD_STATE_MORIBUND;
        }
        if (slot->started) {
            pthread_detach(slot->handle);
        }
    }
    pthread_mutex_unlock(&native_thread_lock);
    return old;
}
void OSCancelThread(OSThread* thread)
{
    NativeThreadSlot* slot;
    pthread_mutex_lock(&native_thread_lock);
    slot = find_thread(thread);
    if (slot != NULL && slot->started) {
        pthread_cancel(slot->handle);
    }
    if (thread != NULL) {
        thread->state = OS_THREAD_STATE_MORIBUND;
    }
    pthread_mutex_unlock(&native_thread_lock);
}
OSThread* OSGetCurrentThread(void) { return native_current_thread; }
s32 OSEnableScheduler(void)
{
    s32 old = native_scheduler_disabled;
    native_scheduler_disabled = 0;
    return old;
}
s32 OSDisableScheduler(void)
{
    s32 old = native_scheduler_disabled;
    native_scheduler_disabled = 1;
    return old;
}
s32 OSCheckActiveThreads(void) { return 0; }

int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                   void* stack, u32 stack_size, s32 priority,
                   unsigned short attr)
{
    size_t i;
    (void) stack;
    (void) stack_size;
    if (thread == NULL || func == NULL) {
        return FALSE;
    }
    memset(thread, 0, sizeof(*thread));
    thread->priority = priority;
    thread->base = priority;
    thread->attr = attr;
    thread->suspend = 1;
    thread->state = OS_THREAD_STATE_READY;
    pthread_mutex_lock(&native_thread_lock);
    for (i = 0; i < sizeof(native_threads) / sizeof(native_threads[0]); i++) {
        if (native_threads[i].thread == NULL) {
            native_threads[i] = (NativeThreadSlot) {
                .thread = thread, .entry = func, .argument = param,
            };
            pthread_mutex_unlock(&native_thread_lock);
            return TRUE;
        }
    }
    pthread_mutex_unlock(&native_thread_lock);
    return FALSE;
}

void OSInitAlarm(void) {}
BOOL OSCheckAlarmQueue(void) { return FALSE; }
void OSCreateAlarm(OSAlarm* alarm)
{
    if (alarm != NULL) {
        memset(alarm, 0, sizeof(*alarm));
    }
}
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    if (alarm != NULL) {
        alarm->fire = OSGetTime() + tick;
        alarm->start = alarm->fire;
        alarm->period = 0;
        alarm->handler = handler;
    }
}
void OSSetAbsAlarm(OSAlarm* alarm, long long time, OSAlarmHandler handler)
{
    if (alarm != NULL) {
        alarm->fire = time;
        alarm->start = time;
        alarm->period = 0;
        alarm->handler = handler;
    }
}
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                        OSAlarmHandler handler)
{
    if (alarm != NULL) {
        alarm->fire = start;
        alarm->start = start;
        alarm->period = period;
        alarm->handler = handler;
    }
}
void OSCancelAlarm(OSAlarm* alarm)
{
    if (alarm != NULL) {
        alarm->handler = NULL;
        alarm->period = 0;
    }
}

u32 OSGetSoundMode(void) { return native_sound_mode; }
void OSSetSoundMode(u32 mode) { native_sound_mode = mode; }
u32 OSGetProgressiveMode(void) { return native_progressive_mode; }
void OSSetProgressiveMode(u32 mode) { native_progressive_mode = mode; }
u32 OSGetResetCode(void) { return native_reset_code; }
void OSResetSystem(int reset, u32 reset_code, BOOL force_menu)
{
    (void) reset;
    (void) force_menu;
    native_reset_code = reset_code;
}
BOOL OSGetResetSwitchState(void) { return FALSE; }
BOOL OSGetResetButtonState(void) { return FALSE; }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td)
{
    time_t seconds;
    struct tm tm_value;
    OSTime second_ticks = OS_TIMER_CLOCK;
    if (td == NULL) {
        return;
    }
    seconds = (time_t) (ticks / second_ticks) + 946684800;
    gmtime_r(&seconds, &tm_value);
    td->sec = tm_value.tm_sec;
    td->min = tm_value.tm_min;
    td->hour = tm_value.tm_hour;
    td->mday = tm_value.tm_mday;
    td->mon = tm_value.tm_mon;
    td->year = tm_value.tm_year + 1900;
    td->wday = tm_value.tm_wday;
    td->yday = tm_value.tm_yday;
    td->msec = (int) ((ticks % second_ticks) * 1000 / second_ticks);
    td->usec = (int) ((ticks % (second_ticks / 1000)) * 1000000 /
                      second_ticks);
}

OSTime OSCalendarTimeToTicks(OSCalendarTime* td)
{
    struct tm tm_value;
    time_t seconds;
    if (td == NULL) {
        return 0;
    }
    tm_value = (struct tm) {
        .tm_sec = td->sec,
        .tm_min = td->min,
        .tm_hour = td->hour,
        .tm_mday = td->mday,
        .tm_mon = td->mon,
        .tm_year = td->year - 1900,
        .tm_isdst = -1,
    };
    seconds = timegm(&tm_value) - 946684800;
    return (OSTime) seconds * OS_TIMER_CLOCK +
           (OSTime) td->msec * (OS_TIMER_CLOCK / 1000) +
           (OSTime) td->usec * (OS_TIMER_CLOCK / 1000000);
}
