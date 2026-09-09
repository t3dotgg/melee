#include "platform/pad.h"

#include <string.h>

/* SI_GC_CONTROLLER from dolphin/si.h. Including that header would also
 * require the hardware OS context declarations, which this host shim does
 * not use. */
#define NATIVE_SI_GC_CONTROLLER 0x09000000u

/*
 * Host-side controller state. The native build does not talk to a GameCube
 * controller directly. A front end can feed this state through the helpers
 * in platform/pad.h, while a headless process gets four disconnected pads.
 */
static PADStatus s_status[PAD_MAX_CONTROLLERS];
static u32 s_spec = PAD_SPEC_5;
static u32 s_sampling_rate;
static BOOL s_initialized;
static BOOL s_disable_recalibration;

static void reset_status(void)
{
    for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        memset(&s_status[i], 0, sizeof(s_status[i]));
        s_status[i].err = PAD_ERR_NO_CONTROLLER;
    }
}

BOOL NativePADSetStatus(s32 chan, const PADStatus* status)
{
    PADInit();
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS || status == NULL) {
        return FALSE;
    }
    s_status[chan] = *status;
    return TRUE;
}

BOOL NativePADSetConnected(s32 chan, BOOL connected)
{
    PADInit();
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return FALSE;
    }
    memset(&s_status[chan], 0, sizeof(s_status[chan]));
    s_status[chan].err = connected ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
    return TRUE;
}

const PADStatus* NativePADGetStatus(s32 chan)
{
    PADInit();
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return NULL;
    }
    return &s_status[chan];
}

BOOL PADInit(void)
{
    if (s_initialized) {
        return TRUE;
    }
    reset_status();
    s_sampling_rate = 0;
    s_initialized = TRUE;
    return TRUE;
}

u32 PADRead(PADStatus* status)
{
    if (!s_initialized) {
        PADInit();
    }
    if (status == NULL) {
        return 0;
    }
    memcpy(status, s_status, sizeof(s_status));

    u32 connected = 0;
    for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        if (status[i].err == PAD_ERR_NONE) {
            connected |= (PAD_CHAN0_BIT >> i);
        }
    }
    return connected;
}

int PADReset(u32 mask)
{
    PADInit();
    for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        if (mask & (PAD_CHAN0_BIT >> i)) {
            s8 error = s_status[i].err;
            memset(&s_status[i], 0, sizeof(s_status[i]));
            s_status[i].err = error;
        }
    }
    return TRUE;
}

BOOL PADRecalibrate(u32 mask)
{
    (void) mask;
    return TRUE;
}

void PADSetSamplingRate(u32 msec)
{
    s_sampling_rate = msec;
}

void __PADTestSamplingRate(u32 tvmode)
{
    (void) tvmode;
}

void PADControlAllMotors(const u32* commandArray)
{
    (void) commandArray;
}

void PADControlMotor(s32 chan, u32 command)
{
    (void) chan;
    (void) command;
}

void PADSetSpec(u32 spec)
{
    s_spec = spec;
}

u32 PADGetSpec(void)
{
    return s_spec;
}

int PADGetType(s32 chan, u32* type)
{
    PADInit();
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS || type == NULL) {
        return FALSE;
    }
    if (s_status[chan].err != PAD_ERR_NONE) {
        *type = 0;
        return FALSE;
    }
    *type = NATIVE_SI_GC_CONTROLLER;
    return TRUE;
}

BOOL PADSync(void)
{
    return TRUE;
}

void PADSetAnalogMode(u32 mode)
{
    (void) mode;
}

BOOL __PADDisableRecalibration(int arg0)
{
    BOOL old = s_disable_recalibration;
    s_disable_recalibration = arg0 != 0;
    return old;
}

void SIRefreshSamplingRate(void)
{
}

void PADClamp(PADStatus* status)
{
    /* The host state is already represented in the same signed ranges as
     * PADStatus. Keep this function as a null-safe compatibility shim. */
    (void) status;
}
