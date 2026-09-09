#include "platform/pad.h"

#include <stdbool.h>
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
static bool s_keyboard_keys[128];

/* US keyboard key codes from NSEvent. Keeping these values here means the
 * controller shim remains a plain C module and can also be driven by tests
 * or another host window implementation. */
enum {
    KEY_A = 0,
    KEY_S = 1,
    KEY_D = 2,
    KEY_W = 13,
    KEY_Q = 12,
    KEY_E = 14,
    KEY_J = 38,
    KEY_K = 40,
    KEY_L = 37,
    KEY_U = 32,
    KEY_I = 34,
    KEY_O = 31,
    KEY_T = 17,
    KEY_F = 3,
    KEY_G = 5,
    KEY_H = 4,
    KEY_Z = 6,
    KEY_X = 7,
    KEY_C = 8,
    KEY_V = 9,
    KEY_RETURN = 36,
    KEY_SPACE = 49,
    KEY_ESCAPE = 53,
    KEY_LEFT = 123,
    KEY_RIGHT = 124,
    KEY_DOWN = 125,
    KEY_UP = 126,
    KEY_SHIFT_LEFT = 56,
    KEY_SHIFT_RIGHT = 60,
    KEY_CONTROL_LEFT = 59,
    KEY_CONTROL_RIGHT = 62,
};

static bool key_down(u16 key_code)
{
    return key_code < (u16) (sizeof(s_keyboard_keys) / sizeof(*s_keyboard_keys)) &&
           s_keyboard_keys[key_code];
}

static void update_keyboard_status(void)
{
    PADStatus* status = &s_status[0];
    u16 buttons = 0;

    if (key_down(KEY_LEFT)) buttons |= PAD_BUTTON_LEFT;
    if (key_down(KEY_RIGHT)) buttons |= PAD_BUTTON_RIGHT;
    if (key_down(KEY_DOWN)) buttons |= PAD_BUTTON_DOWN;
    if (key_down(KEY_UP)) buttons |= PAD_BUTTON_UP;
    if (key_down(KEY_J)) buttons |= PAD_BUTTON_A;
    if (key_down(KEY_K)) buttons |= PAD_BUTTON_B;
    if (key_down(KEY_U)) buttons |= PAD_BUTTON_X;
    if (key_down(KEY_I)) buttons |= PAD_BUTTON_Y;
    if (key_down(KEY_O)) buttons |= PAD_TRIGGER_Z;
    if (key_down(KEY_Q)) buttons |= PAD_TRIGGER_L;
    if (key_down(KEY_E)) buttons |= PAD_TRIGGER_R;
    if (key_down(KEY_RETURN) || key_down(KEY_SPACE)) {
        buttons |= PAD_BUTTON_START;
    }
    status->button = buttons;

    status->stickX = (s8) ((key_down(KEY_D) ? 80 : 0) -
                           (key_down(KEY_A) ? 80 : 0));
    status->stickY = (s8) ((key_down(KEY_W) ? 80 : 0) -
                           (key_down(KEY_S) ? 80 : 0));
    status->substickX = (s8) ((key_down(KEY_H) ? 80 : 0) -
                              (key_down(KEY_F) ? 80 : 0));
    status->substickY = (s8) ((key_down(KEY_T) ? 80 : 0) -
                              (key_down(KEY_G) ? 80 : 0));
    status->triggerLeft = key_down(KEY_SHIFT_LEFT) || key_down(KEY_SHIFT_RIGHT)
                              ? 255
                              : 0;
    status->triggerRight = key_down(KEY_CONTROL_LEFT) ||
                                   key_down(KEY_CONTROL_RIGHT)
                               ? 255
                               : 0;
    status->analogA = key_down(KEY_C) ? 255 : 0;
    status->analogB = key_down(KEY_V) ? 255 : 0;
    status->err = PAD_ERR_NONE;
}

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

void NativePADResetKeyboard(void)
{
    memset(s_keyboard_keys, 0, sizeof(s_keyboard_keys));
    if (s_initialized) {
        memset(&s_status[0], 0, sizeof(s_status[0]));
        s_status[0].err = PAD_ERR_NO_CONTROLLER;
    }
}

void NativePADHandleKeyCode(u16 key_code, BOOL pressed, BOOL repeat)
{
    PADInit();
    if (key_code >= (u16) (sizeof(s_keyboard_keys) / sizeof(*s_keyboard_keys))) {
        return;
    }
    /* A held key must stay down across Cocoa's key-repeat events. */
    if (repeat && !pressed) {
        return;
    }
    s_keyboard_keys[key_code] = pressed != FALSE;
    if (pressed || s_status[0].err == PAD_ERR_NONE) {
        update_keyboard_status();
    }
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
