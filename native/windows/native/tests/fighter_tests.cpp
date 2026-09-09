#include "native_fighter.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace melee::native;
    NativeFighter fighter;
    fighter.update({.stick_x = 1.0F}, 1.0 / 60.0);
    assert(fighter.state().frame == 1);
    assert(fighter.state().action == FighterAction::Run);
    assert(fighter.state().x > 0.09F);

    fighter.update({.jump_pressed = true}, 1.0 / 60.0);
    assert(!fighter.state().grounded && fighter.state().action == FighterAction::Jump);
    const float jump_y = fighter.state().y;
    for (int i = 0; i < 120; ++i) fighter.update({}, 1.0 / 60.0);
    assert(fighter.state().grounded && fighter.state().y == 0.0F);
    assert(jump_y > 0.0F);

    fighter.update({.attack_pressed = true}, 1.0 / 60.0);
    assert(fighter.state().action == FighterAction::Attack);
    for (int i = 0; i < 10; ++i) fighter.update({}, 1.0 / 60.0);
    assert(fighter.state().action == FighterAction::Idle);

    bool rejected = false;
    try { fighter.update({}, 0.0); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "native fighter tests passed\n";
}
