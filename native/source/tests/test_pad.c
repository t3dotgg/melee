#include "platform/pad.h"

#include <stdio.h>

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #condition);                                              \
            failures++;                                                        \
        }                                                                       \
    } while (0)

int main(void)
{
    PADStatus status[PAD_MAX_CONTROLLERS];

    NativePADResetKeyboard();
    CHECK(PADRead(status) == 0);
    CHECK(status[0].err == PAD_ERR_NO_CONTROLLER);

    NativePADHandleKeyCode(38, TRUE, FALSE); /* J, A */
    NativePADHandleKeyCode(2, TRUE, FALSE);  /* D, stick right */
    NativePADHandleKeyCode(56, TRUE, FALSE); /* left shift, L */
    CHECK((PADRead(status) & PAD_CHAN0_BIT) != 0);
    CHECK((status[0].button & PAD_BUTTON_A) != 0);
    CHECK(status[0].stickX == 80);
    CHECK(status[0].triggerLeft == 255);

    NativePADHandleKeyCode(38, FALSE, FALSE);
    NativePADHandleKeyCode(2, FALSE, FALSE);
    NativePADHandleKeyCode(56, FALSE, FALSE);
    CHECK(PADRead(status) & PAD_CHAN0_BIT);
    CHECK(status[0].button == 0);
    CHECK(status[0].stickX == 0);
    CHECK(status[0].triggerLeft == 0);

    NativePADResetKeyboard();
    CHECK(PADRead(status) == 0);
    return failures == 0 ? 0 : 1;
}
