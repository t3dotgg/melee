#include <Runtime/platform.h>

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SDK exports a function with the same name as the macOS macro. */
#undef __assert

#include <melee/gr/grmaterial.h>
#include <melee/gr/types.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itanimlist.h>
#include <melee/it/itcoll.h>
#include <melee/it/iteffect.h>
#include <melee/it/item.h>
#include <melee/it/ithitbox.h>
#include <melee/lb/lb_013B.h>
#include <melee/lb/lb_0219.h>
#include <melee/lb/lbcommand.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>

typedef struct {
    union CmdUnion words[32];
} Script;

static Script* active_script;
static unsigned flash_count;
static unsigned hit_reset_count;
static unsigned hit_scale_count;
static unsigned effect_count;
static unsigned sound_count;
static u32 hit_damage;

HSD_GObjProc* HSD_GObj_CurrentInvokedProc;
Fighter_804D653C_t* it_804D6D04;

/* Fixture targets use byte offsets, as archive command targets do. */
void* native_archive_command_target(const void* word)
{
    const u8* bytes = word;
    uintptr_t address = (uintptr_t) word;
    uintptr_t start = (uintptr_t) active_script;
    u32 offset;
    assert(active_script != NULL);
    assert(address >= start && address + 4 <= start + sizeof(*active_script));
    offset = (u32) bytes[0] << 24 | (u32) bytes[1] << 16 |
             (u32) bytes[2] << 8 | bytes[3];
    assert(offset % 4 == 0 && offset < sizeof(*active_script));
    return (u8*) active_script + offset;
}

static void put(Script* script, unsigned index, u32 value)
{
    u8* bytes = (u8*) &script->words[index];
    assert(index < ARRAY_SIZE(script->words));
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

void lbBgFlash_80021C48(u32 first, u32 second)
{
    assert(first == 0xA7 && second == 0x12345);
    ++flash_count;
}

void it_8026FCF8(Item* item, HitCapsule* hit)
{
    assert(hit == &item->x5D4_hitboxes[3].hit);
    ++hit_reset_count;
}

void it_80272460(HitCapsule* hit, u32 damage, Item_GObj* gobj)
{
    Item* item = gobj->user_data;
    assert(hit == &item->x5D4_hitboxes[3].hit);
    hit_damage = damage;
}

void it_80275594(Item_GObj* gobj, s32 index, float scale)
{
    assert(gobj->user_data != NULL && index == 3);
    near(scale, 0.5f);
    ++hit_scale_count;
}

void it_80278800(Item_GObj* gobj, s32 effect, s32 bone, Vec3* offset,
                 Vec3* range, s32 unknown, float value)
{
    assert(gobj->user_data != NULL);
    assert(effect == 0x3456 && bone == 0x2AB && unknown == 0);
    near(value, 0x789A);
    near(offset->x, -291 * 0.003906f);
    near(offset->y, 1110 * 0.003906f);
    near(offset->z, -1929 * 0.003906f);
    near(range->x, 2748 * 0.003906f);
    near(range->y, -3567 * 0.003906f);
    near(range->z, 4660 * 0.003906f);
    ++effect_count;
}

void Item_8026AF0C(Item* item, enum_t sfx, u8 pan, u8 volume)
{
    assert(item != NULL);
    assert(sfx == 0x12345678 && pan == 0x9A && volume == 0xBC);
    ++sound_count;
}

/* Unexpected platform operations fail instead of concealing dispatch errors.
 */
void OSReport(char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void(__assert)(char* file, u32 line, char* message)
{
    fprintf(stderr, "%s:%u: %s\n", file, line, message);
    abort();
}

void it_8027129C(Item_GObj* gobj, s32 index)
{
    abort();
}

void it_80272560(Item_GObj* gobj, s32 index)
{
    abort();
}

void it_802725D4(Item_GObj* gobj)
{
    abort();
}

void it_80273598(Item_GObj* gobj, s32 first, s32 second)
{
    abort();
}

void it_80273600(Item_GObj* gobj)
{
    abort();
}

void it_80273648(Item_GObj* gobj, s32 first, s32 second)
{
    abort();
}

void Item_8026AE84(Item* item, enum_t sound, u8 pan, u8 volume)
{
    abort();
}

void Item_8026AFA0(Item* item, enum_t sound, u8 pan, u8 volume)
{
    abort();
}

void Item_8026B034(Item* item)
{
    abort();
}

void Item_8026B074(Item* item)
{
    abort();
}

static void test_common_control_flow(void)
{
    Script script = { 0 };
    CommandInfo cmd = { 0 };
    unsigned steps = 0;
    unsigned maximum_depth = 0;

    put(&script, 0, 5U << 26); /* Call a function with two nested loops. */
    put(&script, 1, 8 * 4);
    put(&script, 2, (2U << 26) | 19);
    put(&script, 3, 0);
    put(&script, 8, (3U << 26) | 2);
    put(&script, 9, (3U << 26) | 3);
    put(&script, 10, 7U << 26);
    put(&script, 11, 16 * 4);
    put(&script, 16, (9U << 26) | (0xA7 << 18) | 0x12345);
    put(&script, 17, (1U << 26) | 2);
    put(&script, 18, 4U << 26);
    put(&script, 19, 4U << 26);
    put(&script, 20, 6U << 26);
    active_script = &script;
    cmd.u = script.words;
    cmd.timer = -0.25f;
    cmd.frame_count = 7.5f;
    assert((uintptr_t) cmd.u > UINT32_MAX);

    while (cmd.u != NULL) {
        u32 opcode = ((u8*) cmd.u)[0] >> 2;
        assert(++steps < 100);
        assert(Command_Execute(&cmd, opcode));
        if (cmd.loop_count > maximum_depth) {
            maximum_depth = cmd.loop_count;
        }
        if (opcode == 1) {
            near(cmd.timer, flash_count * 2 - 0.25f);
        }
    }
    assert(maximum_depth == 5);
    assert(cmd.loop_count == 0 && flash_count == 6);
    near(cmd.timer, 11.5f);

    cmd.u = script.words;
    cmd.timer = 0.25f;
    assert(!Command_Execute(&cmd, 10));
    assert(cmd.u == script.words && cmd.timer == 0.25f);
    assert(Command_Execute(&cmd, 8));
    assert(cmd.timer == F32_MAX && cmd.u == script.words + 1);
}

static void test_item_frames(void)
{
    Script script = { 0 };
    Item item = { 0 };
    HSD_GObj gobj = { 0 };

    put(&script, 0, (17U << 26) | 0x123456);
    put(&script, 1, (1U << 26) | 2);
    put(&script, 2, (18U << 26) | 0x234567);
    put(&script, 3, (2U << 26) | 7);
    put(&script, 4, (19U << 26) | 0x312456);
    put(&script, 5, 8U << 26);
    put(&script, 6, 20U << 26);
    put(&script, 7, 0);
    item.x524_cmd.u = script.words;
    item.x5D0_animFrameSpeed = 1;
    gobj.user_data = &item;

    it_802799E4(&gobj);
    assert(item.xDAC_itcmd_var0 == 0x123456);
    assert(item.xDB0_itcmd_var1 == 0 && item.xDB4_itcmd_var2 == 0);
    near(item.x524_cmd.timer, 1);
    item.x5CC_currentAnimFrame = 1;
    it_802799E4(&gobj);
    assert(item.xDB0_itcmd_var1 == 0x234567);
    near(item.x524_cmd.timer, 6);
    for (unsigned frame = 2; frame < 7; ++frame) {
        item.x5CC_currentAnimFrame = frame;
        it_802799E4(&gobj);
        assert(item.xDB4_itcmd_var2 == 0);
    }
    item.x5CC_currentAnimFrame = 7;
    it_802799E4(&gobj);
    assert(item.xDB4_itcmd_var2 == 0x312456);
    assert(item.x524_cmd.timer == F32_MAX);
    assert(item.x524_cmd.u == script.words + 6);
    item.x5CC_currentAnimFrame = 8;
    it_802799E4(&gobj);
    assert(!item.xDBC_itcmd_var4.flags.x0);
    item.x5CC_currentAnimFrame = 0;
    it_802799E4(&gobj);
    assert(item.xDBC_itcmd_var4.flags.x0);
    assert(item.x524_cmd.u == NULL);
    near(item.x524_cmd.timer, 0);
    it_802799E4(&gobj);
    assert(!item.xDBC_itcmd_var4.flags.x0);
}

static void test_item_hitbox(void)
{
    Script script = { 0 };
    Item item = { 0 };
    HSD_GObj gobj = { 0 };
    HSD_JObj joint = { 0 };
    HitCapsule* hit = &item.x5D4_hitboxes[3].hit;

    put(&script, 0, (11U << 26) | (3U << 23) | (5U << 20) | 0x1234);
    put(&script, 1, (0x1234U << 16) | (u16) -4660);
    put(&script, 2, ((u32) (u16) -321 << 16) | 0x2345);
    put(&script, 3, (301U << 23) | (257U << 14) | (113U << 5));
    put(&script, 4,
        (293U << 23) | (13U << 18) | (1U << 17) | ((u32) (u8) -37 << 9) |
            (5U << 6) | (11U << 2) | 1);
    put(&script, 5, 0x01A6D400);
    put(&script, 6, 0);
    item.xC3C = 2;
    item.xC40 = 0.5f;
    item.scl = 2;
    item.x524_cmd.u = script.words;
    item.x5D0_animFrameSpeed = 1;
    gobj.user_data = &item;
    gobj.hsd_obj = &joint;

    it_802799E4(&gobj);
    assert(item.x524_cmd.u == NULL);
    assert(hit_reset_count == 1 && hit_scale_count == 1);
    assert(hit->state == HitCapsule_Enabled && hit->x4 == 5);
    assert(hit->jobj == &joint && hit_damage == 0x1234);
    assert(item.xDC8_word.flags.x16 && !item.xDAA_flag.b2);
    near(hit->scale, 0x1234 * 0.003906f);
    near(item.x3C, hit->scale);
    near(hit->b_offset.x, -4660 * 0.003906f);
    near(hit->b_offset.y, -321 * 0.003906f);
    near(hit->b_offset.z, 0x2345 * 0.003906f);
    assert(hit->kb_angle == 301 && hit->x24 == 257 && hit->x28 == 113);
    assert(hit->x2C == 293 && hit->element == 13 && hit->x34 == -37);
    assert(hit->sfx_severity == 5 && hit->sfx_kind == 11);
    assert(hit->x40_b0 && !hit->x40_b1 && hit->x40_b2 && !hit->x40_b3);
    assert(hit->x40_b4 && hit->x41_b4 && !hit->x41_b5 && hit->x41_b6);
    assert(!hit->x41_b7 && !hit->x42_b0 && hit->x42_b1 && hit->x42_b2);
    assert(!hit->x42_b3 && hit->x42_b4 && hit->x42_b5 && !hit->x42_b6);
    assert(hit->x42_b7 && !hit->x43_b0 && item.x5D4_hitboxes[3].x138);
    assert(!hit->x43_b1 && !hit->x43_b2);
}

static void test_item_effect_and_sound(void)
{
    Script script = { 0 };
    Item item = { 0 };
    HSD_GObj gobj = { 0 };

    put(&script, 0, (10U << 26) | (0x2ABU << 16));
    put(&script, 1, 0x3456789A);
    put(&script, 2, ((u32) (u16) -291 << 16) | 1110);
    put(&script, 3, ((u32) (u16) -1929 << 16) | 2748);
    put(&script, 4, ((u32) (u16) -3567 << 16) | 4660);
    put(&script, 5, (16U << 26) | (1U << 18));
    put(&script, 6, 0x12345678);
    put(&script, 7, 0x00009ABC);
    put(&script, 8, 0);
    item.x524_cmd.u = script.words;
    item.x5D0_animFrameSpeed = 1;
    gobj.user_data = &item;
    it_802799E4(&gobj);
    assert(item.x524_cmd.u == NULL && effect_count == 1 && sound_count == 1);
}

static void test_color_control_flow(void)
{
    Script script = { 0 };
    ColorOverlay color = { 0 };

    put(&script, 0, 5U << 26);
    put(&script, 1, 8 * 4);
    put(&script, 2, (11U << 26) | 3);
    put(&script, 3, 0);
    put(&script, 8, (3U << 26) | 2);
    put(&script, 9,
        (13U << 26) | (1U << 25) | ((u32) (-321 & 0xFFF) << 12) | 745);
    put(&script, 10, 0x12345678);
    put(&script, 11, (11U << 26) | 1);
    put(&script, 12, (16U << 26) | ((u32) (-2047 & 0x1FFF) << 13) | 1303);
    put(&script, 13, 18U << 26);
    put(&script, 14, 0x9ABCDEEF);
    put(&script, 15, (11U << 26) | 1);
    put(&script, 16, 4U << 26);
    put(&script, 17, 6U << 26);
    color.x8_ptr1 = (union ColorOverlay_x8_t*) script.words;
    active_script = &script;

    assert(!lb_80014258(NULL, &color, NULL));
    assert(color.xC_loop == 3 && color.x0_timer == 1);
    assert(((CommandInfo*) &color)->event_return[0] == script.words + 2);
    assert(((CommandInfo*) &color)->event_return[1] == script.words + 9);
    assert((uintptr_t) ((CommandInfo*) &color)->event_return[2] == 2);
    assert(color.x7C_light_enable && color.x7C_flag2 &&
           !color.x7C_color_enable);
    near(color.x74_light_rot_x, -321);
    near(color.x78_light_rot_yz, 745);
    assert(color.x50_light_color.r == 0x12 && color.x50_light_color.g == 0x34);
    assert(color.x50_light_color.b == 0x56 && color.x50_light_color.a == 0x78);
    assert(!lb_80014258(NULL, &color, NULL));
    assert(color.xC_loop == 3 && color.x0_timer == 1);
    assert(color.x7C_color_enable);
    near(color.x74_light_rot_x, -2047);
    near(color.x78_light_rot_yz, 1303);
    assert(color.x2C_hex.r == 0x9A && color.x2C_hex.g == 0xBC);
    assert(color.x2C_hex.b == 0xDE && color.x2C_hex.a == 0xEF);
    assert(!lb_80014258(NULL, &color, NULL));
    assert(color.xC_loop == 3 && color.x0_timer == 1);
    assert((uintptr_t) ((CommandInfo*) &color)->event_return[2] == 1);
    near(color.x74_light_rot_x, -321);
    assert(!lb_80014258(NULL, &color, NULL));
    near(color.x74_light_rot_x, -2047);
    assert(!lb_80014258(NULL, &color, NULL));
    assert(color.xC_loop == 0 && color.x0_timer == 3);
    for (unsigned frame = 0; frame < 3; ++frame) {
        assert(!lb_80014258(NULL, &color, NULL));
    }
    assert(color.x8_ptr1 == NULL && color.x0_timer == 0);
    lb_80014498(&color);
    assert(!color.x7C_color_enable && !color.x7C_flag2);
}

static void test_stage_color_extension(void)
{
    Script script = { 0 };
    Ground ground = { 0 };
    HSD_GObj gobj = { 0 };
    ColorOverlay* color = &ground.color_overlay;

    put(&script, 0, (21U << 26) | (0xB3U << 18));
    put(&script, 1, (11U << 26) | 2);
    put(&script, 2, 10U << 26);
    color->x8_ptr1 = (union ColorOverlay_x8_t*) script.words;
    gobj.user_data = &ground;
    assert(!lb_80014258(&gobj, color, fn_801C9664));
    near(ground.xC0, 0xB3);
    assert(ground.x10_flags.b6 && color->x0_timer == 2);
    assert(color->x8_ptr1 == (union ColorOverlay_x8_t*) (script.words + 2));
    assert(!lb_80014258(&gobj, color, fn_801C9664));
    assert(color->x0_timer == 1);
    assert(lb_80014258(&gobj, color, fn_801C9664));
    assert(color->x0_timer == 0);
}

int main(void)
{
    assert(sizeof(union CmdUnion) == 4);
    test_common_control_flow();
    test_item_frames();
    test_item_hitbox();
    test_item_effect_and_sound();
    test_color_control_flow();
    test_stage_color_extension();
    puts("Common, item, and color command tests passed.");
    return 0;
}
