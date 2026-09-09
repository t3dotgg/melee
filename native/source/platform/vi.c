#include "card.h"
#include "os.h"
#include "pad.h"
#include "scheduler.h"
#include <dolphin/vi.h>

#if defined(MELEE_NATIVE_DISPLAY)
#include "audio_output.h"
#include "display.h"
extern void NativeAudioTick(void) __attribute__((weak_import));
#endif

#include <string.h>

typedef struct NativeVIState {
    GXRenderModeObj mode;
    void* next_frame_buffer;
    void* next_right_frame_buffer;
    VIRetraceCallback pre_callback;
    VIRetraceCallback post_callback;
    u32 retrace_count;
    u32 next_field;
    u32 current_line;
    BOOL black;
    BOOL three_d;
    BOOL initialized;
} NativeVIState;

static NativeVIState s_vi;

void VIInit(void)
{
    memset(&s_vi, 0, sizeof(s_vi));
    s_vi.mode.viTVmode = VI_TVMODE_NTSC_INT;
    s_vi.mode.fbWidth = 640;
    s_vi.mode.efbHeight = 480;
    s_vi.mode.xfbHeight = 480;
    s_vi.mode.viWidth = 640;
    s_vi.mode.viHeight = 480;
    s_vi.black = TRUE;
    s_vi.initialized = TRUE;
}

static void ensure_initialized(void)
{
    if (!s_vi.initialized) {
        VIInit();
    }
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback)
{
    ensure_initialized();
    VIRetraceCallback old = s_vi.pre_callback;
    s_vi.pre_callback = callback;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback)
{
    ensure_initialized();
    VIRetraceCallback old = s_vi.post_callback;
    s_vi.post_callback = callback;
    return old;
}

void VIWaitForRetrace(void)
{
    ensure_initialized();
    NativeSchedulerWaitForRetrace();
    NativeSchedulerPump(NativeSchedulerGetTime());
    if (NativeOSInterruptsEnabled()) {
        NativeCardPump();
    }
    ++s_vi.retrace_count;
    s_vi.next_field ^= 1;

#if defined(MELEE_NATIVE_DISPLAY)
    /* Audio mixing is optional in headless builds and may be absent from
     * small platform tests. */
    if (NativeAudioTick != NULL) {
        NativeAudioTick();
    }
#endif
    NativePADAdvanceFrame(s_vi.retrace_count);

    /* Hardware invokes these callbacks around each vertical retrace. A
     * synchronous call gives headless builds the same ordering without
     * creating a platform-specific display thread. */
    VIRetraceCallback pre = s_vi.pre_callback;
    if (pre != NULL) {
        pre(s_vi.retrace_count);
    }
    VIRetraceCallback post = s_vi.post_callback;
    if (post != NULL) {
        post(s_vi.retrace_count);
    }

    /* The GameCube presents the XFB selected during the retrace. The native
     * backend copies that RGB565 buffer to a Cocoa window synchronously. */
#if defined(MELEE_NATIVE_DISPLAY)
    NativeDisplaySetRetraceCount(s_vi.retrace_count);
    if (!s_vi.black && s_vi.next_frame_buffer != NULL) {
        uint16_t width =
            s_vi.mode.viWidth != 0 ? s_vi.mode.viWidth : s_vi.mode.fbWidth;
        uint16_t height =
            s_vi.mode.viHeight != 0 ? s_vi.mode.viHeight : s_vi.mode.xfbHeight;
        uint16_t stride = (uint16_t) ((s_vi.mode.fbWidth + 15u) & ~15u);
        NativeDisplayPresent(s_vi.next_frame_buffer, width, height, stride);
    }
    /* Input events must continue to reach the pad shim while the VI is
     * presenting frames. NativeDisplayPresent also pumps events, but this
     * call keeps the input path explicit for a future asynchronous renderer.
     */
    NativeDisplayPumpEvents();
#endif
}

void VIConfigure(GXRenderModeObj* mode)
{
    ensure_initialized();
    if (mode != NULL) {
        s_vi.mode = *mode;
    }
}

void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height)
{
    ensure_initialized();
    s_vi.mode.viXOrigin = xOrg;
    s_vi.mode.viYOrigin = yOrg;
    s_vi.mode.viWidth = width;
    s_vi.mode.viHeight = height;
}

void VIFlush(void)
{
    ensure_initialized();
}

void VISetNextFrameBuffer(void* frame_buffer)
{
    ensure_initialized();
    s_vi.next_frame_buffer = frame_buffer;
}

void VISetNextRightFrameBuffer(void* frame_buffer)
{
    ensure_initialized();
    s_vi.next_right_frame_buffer = frame_buffer;
}

void VISetBlack(BOOL black)
{
    ensure_initialized();
    s_vi.black = black;
}

void VISet3D(BOOL three_d)
{
    ensure_initialized();
    s_vi.three_d = three_d;
}

u32 VIGetRetraceCount(void)
{
    ensure_initialized();
    return s_vi.retrace_count;
}

u32 VIGetNextField(void)
{
    ensure_initialized();
    return s_vi.next_field;
}

u32 VIGetCurrentLine(void)
{
    ensure_initialized();
    return s_vi.current_line;
}

u32 VIGetTvFormat(void)
{
    ensure_initialized();
    return ((u32) s_vi.mode.viTVmode) >> 2;
}

u32 VIGetDTVStatus(void)
{
    return 0;
}
