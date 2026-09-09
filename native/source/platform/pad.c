#include <dolphin/pad.h>

#include <string.h>

/*
 * Host-side controller state. The native build does not talk to a GameCube
 * controller directly. A front end can feed this state through the helpers
 * in platform/pad.h, while a headless process gets four disconnected pads.
 */
static PADStatus s_status[PAD_MAX_CONTROLLERS];
static u32 s_spec;
static u32 s_sampling_rate;
static BOOL s_initialized;

static void reset_status(void)
{
    for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        memset(&s_status[i], 0, sizeof(s_status[i]));
        s_status[i].err = PAD_ERR_NO_CONTROLLER;
    }
}

BOOL NativePADSetStatus(s32 chan, const PADStatus* status)
{
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS || status == NULL) {
        return FALSE;
    }
    s_status[chan] = *status;
    return TRUE;
}

BOOL NativePADSetConnected(s32 chan, BOOL connected)
{
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return FALSE;
    }
    s_status[chan].err = connected ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
    return TRUE;
}

const PADStatus* NativePADGetStatus(s32 chan)
{
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return NULL;
    }
    return &s_status[chan];
}

BOOL PADInit(void)
{
    reset_status();
    s_spec = PAD_SPEC_0;
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
    for (int i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        if (mask & (PAD_CHAN0_BIT >> i)) {
            /* Reset keeps the controller disconnected until a front end
             * provides a new status. */
            s_status[i].button = 0;
            s_status[i].stickX = 0;
            s_status[i].stickY = 0;
            s_status[i].substickX = 0;
            s_status[i].substickY = 0;
            s_status[i].triggerLeft = 0;
            s_status[i].triggerRight = 0;
            s_status[i].analogA = 0;
            s_status[i].analogB = 0;
            s_status[i].err = PAD_ERR_NO_CONTROLLER;
        }
    }
    return 0;
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
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS || type == NULL) {
        return PAD_ERR_NO_CONTROLLER;
    }
    if (s_status[chan].err != PAD_ERR_NONE) {
        *type = 0;
        return PAD_ERR_NO_CONTROLLER;
    }
    *type = 0;
    return PAD_ERR_NONE;
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
    (void) arg0;
    return TRUE;
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
