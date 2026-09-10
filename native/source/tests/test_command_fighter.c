#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* The SDK exports a function with the same name as the macOS macro. */
#undef __assert

/* Test the real private handlers without the full game dispatch. */
#include <melee/ft/ftaction.c>

static unsigned model_calls;
static unsigned texture_calls;
static unsigned damage_calls;

void ftParts_80074B0C(Fighter_GObj* gobj, int model, int value)
{
    assert(gobj->user_data != NULL);
    assert(model == -37 && value == -0x12345);
    ++model_calls;
}

void ftAnim_800704F0(Fighter_GObj* gobj, int texture, float frame)
{
    assert(gobj->user_data != NULL);
    assert(texture == (texture_calls == 0 ? -51 : 37));
    assert(frame == -713);
    ++texture_calls;
}

void Fighter_TakeDamage_8006CC7C(Fighter* fighter, float damage)
{
    assert(fighter != NULL);
    assert(damage == -0x123456);
    ++damage_calls;
}

static void put(union CmdUnion* word, u32 value)
{
    u8* bytes = (u8*) word;
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

static void test_fighter_variables(void)
{
    Fighter fighter = { 0 };
    HSD_GObj gobj = { 0 };
    CommandInfo cmd = { 0 };
    union CmdUnion words[4];

    gobj.user_data = &fighter;
    cmd.u = words;
    for (unsigned index = 0; index < ARRAY_SIZE(words); ++index) {
        put(&words[index], (19U << 26) | (index << 24) | (0x123456 + index));
    }
    for (unsigned index = 0; index < ARRAY_SIZE(words); ++index) {
        ftAction_80071820(&gobj, &cmd);
        assert(cmd.u == words + index + 1);
        for (unsigned check = 0; check < ARRAY_SIZE(words); ++check) {
            assert(fighter.cmd_vars[check] ==
                   (check <= index ? 0x123456 + check : 0));
        }
    }
}

static void test_signed_fighter_fields(void)
{
    Fighter fighter = { 0 };
    HSD_GObj gobj = { 0 };
    CommandInfo cmd = { 0 };
    union CmdUnion words[3];

    put(&words[0],
        (31U << 26) | ((u32) (-37 & 0x7F) << 19) | (-0x12345 & 0x7FFFF));
    put(&words[1], (40U << 26) | (1U << 25) | ((u32) (-51 & 0x7F) << 18) |
                       (37U << 11) | (-713 & 0x7FF));
    put(&words[2], (51U << 26) | (-0x123456 & 0x3FFFFFF));
    gobj.user_data = &fighter;
    cmd.u = words;
    ftAction_80071D40(&gobj, &cmd);
    assert(cmd.u == words + 1 && model_calls == 1);
    ftAction_800726F4(&gobj, &cmd);
    assert(cmd.u == words + 2 && texture_calls == 2);
    ftAction_80072BF4(&gobj, &cmd);
    assert(cmd.u == words + 3 && damage_calls == 1);
}

int main(void)
{
    test_fighter_variables();
    test_signed_fighter_fields();
    puts("Fighter command tests passed.");
    return 0;
}
