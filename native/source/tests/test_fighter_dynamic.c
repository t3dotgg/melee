#include <assert.h>
#include <stddef.h>

#include <melee/ft/types.h>

int main(void)
{
    Fighter fighter = { 0 };

    assert(ARRAY_SIZE(fighter.x1670) == 11);
    assert(sizeof(Fighter_x1670_t) == 48);

    for (size_t i = 0; i < ARRAY_SIZE(fighter.x1670); i++) {
        fighter.x1670[i].v1.x = (float) i;
        fighter.x1670[i].x24 = (int) i;
    }

    assert(fighter.x1670[10].v1.x == 10.0f);
    assert(fighter.x1670[10].x24 == 10);
    return 0;
}
