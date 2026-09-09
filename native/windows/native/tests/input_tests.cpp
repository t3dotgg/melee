#include "native_input.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace melee::native;

int main()
{
    XboxPadMapper mapper;
    XInputState sample{};
    sample.buttons = xinput_button::a | xinput_button::x;
    sample.thumb_lx = 32767;
    sample.thumb_ly = -32768;
    sample.left_trigger = 255;

    const PadState first = mapper.update(sample);
    assert(first.held(PadButton::Attack));
    assert(first.held(PadButton::Jump));
    assert(first.pressed(PadButton::Attack));
    assert(first.pressed(PadButton::Jump));
    assert(first.held(PadButton::Shield));
    assert(first.pressed(PadButton::Shield));
    assert(first.stick_x > 0.99F);
    assert(first.stick_y < -0.99F);
    assert(std::fabs(first.left_trigger - 1.0F) < 0.001F);

    // Holding a button does not retrigger it; adding B reports only B pressed.
    sample.buttons = xinput_button::a | xinput_button::b | xinput_button::y;
    sample.left_trigger = 0;
    const PadState second = mapper.update(sample);
    assert(second.held(PadButton::Attack));
    assert(second.held(PadButton::Special));
    assert(second.held(PadButton::Jump));
    assert(!second.pressed(PadButton::Attack));
    assert(second.pressed(PadButton::Special));
    assert(!second.pressed(PadButton::Shield));
    assert(second.released(PadButton::Shield));

    // Releasing all controls exposes release edges exactly once.
    const PadState third = mapper.update({});
    assert(third.held_mask == 0);
    assert(third.released(PadButton::Attack));
    assert(third.released(PadButton::Special));
    assert(third.released(PadButton::Jump));
    assert(!third.released(PadButton::Attack) || third.released_mask != 0);
    const PadState fourth = mapper.update({});
    assert(fourth.pressed_mask == 0 && fourth.released_mask == 0);

    mapper.reset();
    const PadState after_reset = mapper.update(sample);
    assert(after_reset.pressed(PadButton::Attack));
    std::cout << "native input mapping tests passed\n";
}
