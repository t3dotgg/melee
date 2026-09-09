#include <melee/ft/forward.h>

#include "forward.h"
#include "inlines.h"
#include "lbarchive.h"
#include "lbcommand.h"
#include "types.h"
#include <dolphin/pad.h>
#include <sysdolphin/baselib/rumble.h>

typedef bool (*lb_803BA248_fn)(ColorOverlay*);
/* 013BB8 */ static bool lb_80013BB8(ColorOverlay* overlay);
/* 013BE4 */ static bool lb_80013BE4(ColorOverlay* overlay);
/* 013FF0 */ static bool lb_80013FF0(ColorOverlay* overlay);
/* 014234 */ static bool lb_80014234(ColorOverlay* overlay);

static struct Fighter_804D653C_t* lb_804D63C0;

// Return completion before the per-frame color update.
bool lb_80013BB0(ColorOverlay* overlay)
{
    return true;
}

bool lb_80013BB8(ColorOverlay* overlay)
{
    overlay->x0_timer += CMD_FIELD(overlay->x8_ptr1, unk, timer);
    ++overlay->x8_ptr1;
    return false;
}

bool lb_80013BE4(ColorOverlay* overlay)
{
    overlay->x7C_color_enable = overlay->x7C_flag2 = false;
    ++overlay->x8_ptr1;
    return false;
}

// Read one RGBA word, initialize the float channels, and clear their steps.
static inline void readLightColor(ColorOverlay* overlay)
{
    overlay->x50_light_color.r = overlay->x8_ptr1->light_color.r;
    overlay->x50_light_color.g = overlay->x8_ptr1->light_color.g;
    overlay->x50_light_color.b = overlay->x8_ptr1->light_color.b;
    overlay->x50_light_color.a = overlay->x8_ptr1->light_color.a;
    overlay->x54_light_red = overlay->x50_light_color.r;
    overlay->x58_light_green = overlay->x50_light_color.g;
    overlay->x5C_light_blue = overlay->x50_light_color.b;
    overlay->x60_light_alpha = overlay->x50_light_color.a;
    overlay->x70_lightblend_alpha = 0.0f;
    overlay->x6C_lightblend_blue = 0.0f;
    overlay->x68_lightblend_green = 0.0f;
    overlay->x64_lightblend_red = 0.0f;
    ++overlay->x8_ptr1;
}

static bool lb_80013C18(ColorOverlay* overlay)
{
    overlay->x7C_light_enable =
        CMD_FIELD(overlay->x8_ptr1, light_rot2, light_enable);
    overlay->x74_light_rot_x = CMD_FIELD(overlay->x8_ptr1, light_rot2, x);
    overlay->x78_light_rot_yz = CMD_FIELD(overlay->x8_ptr1, light_rot2, yz);
    ++overlay->x8_ptr1;
    readLightColor(overlay);
    overlay->x7C_flag2 = true;
    return false;
}

static bool lb_80013D68(ColorOverlay* overlay)
{
    ++overlay->x8_ptr1;
    readLightColor(overlay);
    return false;
}

// The command word supplies the duration. The next word supplies RGBA.
static bool lb_80013E3C(ColorOverlay* overlay)
{
    float blend_frames = CMD_FIELD(overlay->x8_ptr1++, unk, timer);
    overlay->x64_lightblend_red = ((0.5f + overlay->x8_ptr1->light_color.r) -
                                   overlay->x50_light_color.r) /
                                  blend_frames;
    overlay->x68_lightblend_green = ((0.5f + overlay->x8_ptr1->light_color.g) -
                                     overlay->x50_light_color.g) /
                                    blend_frames;
    overlay->x6C_lightblend_blue = ((0.5f + overlay->x8_ptr1->light_color.b) -
                                    overlay->x50_light_color.b) /
                                   blend_frames;
    overlay->x70_lightblend_alpha = ((0.5f + overlay->x8_ptr1->light_color.a) -
                                     overlay->x50_light_color.a) /
                                    blend_frames;
    ++overlay->x8_ptr1;
    return false;
}

static bool lb_80013F78(ColorOverlay* overlay)
{
    overlay->x74_light_rot_x = CMD_FIELD(overlay->x8_ptr1, light_rot1, x);
    overlay->x78_light_rot_yz = CMD_FIELD(overlay->x8_ptr1, light_rot1, yz);
    ++overlay->x8_ptr1;
    return false;
}

static bool lb_80013FF0(ColorOverlay* overlay)
{
    overlay->x7C_flag2 = false;
    ++overlay->x8_ptr1;
    return false;
}

static bool lb_80014014(ColorOverlay* overlay)
{
    overlay->x7C_color_enable = true;
    ++overlay->x8_ptr1;
    overlay->x2C_hex.r = overlay->x8_ptr1->light_color.r;
    overlay->x2C_hex.g = overlay->x8_ptr1->light_color.g;
    overlay->x2C_hex.b = overlay->x8_ptr1->light_color.b;
    overlay->x2C_hex.a = overlay->x8_ptr1->light_color.a;
    overlay->x30_color_red = overlay->x2C_hex.r;
    overlay->x34_color_green = overlay->x2C_hex.g;
    overlay->x38_color_blue = overlay->x2C_hex.b;
    overlay->x3C_color_alpha = overlay->x2C_hex.a;
    overlay->x4C_colorblend_alpha = 0.0f;
    overlay->x48_colorblend_blue = 0.0f;
    overlay->x44_colorblend_green = 0.0f;
    overlay->x40_colorblend_red = 0.0f;
    ++overlay->x8_ptr1;
    return false;
}

static bool lb_800140F8(ColorOverlay* overlay)
{
    float blend_frames = CMD_FIELD(overlay->x8_ptr1++, unk, timer);
    overlay->x40_colorblend_red =
        ((0.5f + overlay->x8_ptr1->light_color.r) - overlay->x2C_hex.r) /
        blend_frames;
    overlay->x44_colorblend_green =
        ((0.5f + overlay->x8_ptr1->light_color.g) - overlay->x2C_hex.g) /
        blend_frames;
    overlay->x48_colorblend_blue =
        ((0.5f + overlay->x8_ptr1->light_color.b) - overlay->x2C_hex.b) /
        blend_frames;
    overlay->x4C_colorblend_alpha =
        ((0.5f + overlay->x8_ptr1->light_color.a) - overlay->x2C_hex.a) /
        blend_frames;
    ++overlay->x8_ptr1;
    return false;
}

bool lb_80014234(ColorOverlay* overlay)
{
    overlay->x7C_color_enable = false;
    ++overlay->x8_ptr1;
    return false;
}

lb_803BA248_fn lb_803BA248[] = {
    lb_80013BB0, lb_80013BB8, lb_80013BE4, lb_80013C18, lb_80013D68,
    lb_80013E3C, lb_80013F78, lb_80013FF0, lb_80014014, lb_800140F8,
    lb_80014234, NULL,        NULL,        NULL,        NULL,
    NULL,        NULL,        NULL,        NULL,        NULL,
    NULL,        NULL,
};

bool lb_80014258(Fighter_GObj* gobj, void* overlay_data, FtCmd2 execute_cmd)
{
    ColorOverlay* overlay = overlay_data;

    if (overlay->x8_ptr1 != NULL) {
        s32 timer = overlay->x0_timer;
        if (timer != 0) {
            overlay->x0_timer = timer - 1;
        }
    }

    while (overlay->x8_ptr1 != NULL && overlay->x0_timer == 0) {
        u32 opcode = CMD_FIELD(overlay->x8_ptr1, unk, unk);
        if (!Command_Execute((CommandInfo*) overlay, opcode)) {
            if (opcode < 0x15U) {
                u32 handler_index = opcode - 0xA;
                if (lb_803BA248[handler_index](overlay)) {
                    return true;
                }
            } else {
                execute_cmd(gobj, (CommandInfo*) overlay, (int) opcode);
            }
        }
    }

    if (overlay->x7C_color_enable) {
        overlay->x30_color_red += overlay->x40_colorblend_red;
        overlay->x34_color_green += overlay->x44_colorblend_green;
        overlay->x38_color_blue += overlay->x48_colorblend_blue;
        overlay->x3C_color_alpha += overlay->x4C_colorblend_alpha;
        overlay->x2C_hex.r = (u8) overlay->x30_color_red;
        overlay->x2C_hex.g = (u8) overlay->x34_color_green;
        overlay->x2C_hex.b = (u8) overlay->x38_color_blue;
        overlay->x2C_hex.a = (u8) overlay->x3C_color_alpha;
    }
    if (overlay->x7C_flag2) {
        overlay->x54_light_red += overlay->x64_lightblend_red;
        overlay->x58_light_green += overlay->x68_lightblend_green;
        overlay->x5C_light_blue += overlay->x6C_lightblend_blue;
        overlay->x60_light_alpha += overlay->x70_lightblend_alpha;
        overlay->x50_light_color.r = (u8) overlay->x54_light_red;
        overlay->x50_light_color.g = (u8) overlay->x58_light_green;
        overlay->x50_light_color.b = (u8) overlay->x5C_light_blue;
        overlay->x50_light_color.a = (u8) overlay->x60_light_alpha;
    }
    {
        // x4_pri is a duration here. The animation table holds the priority.
        s32 remaining_frames = overlay->x4_pri;
        if (remaining_frames != 0) {
            overlay->x4_pri = remaining_frames - 1;
            if (overlay->x4_pri == 0) {
                return true;
            }
        }
    }
    return false;
}

void lb_80014498(ColorOverlay* overlay)
{
    overlay->x8_ptr1 = NULL;
    overlay->x4_pri = 0;
    overlay->x28_colanim.ptr = NULL;
    overlay->x7C_color_enable = overlay->x7C_flag2 = false;
}

bool lb_800144C8(ColorOverlay* overlay, Fighter_804D653C_t* animations,
                 int animation_id, int duration)
{
    if (animations[overlay->x28_colanim.i].unk4 <=
        animations[animation_id].unk4)
    {
        overlay->x28_colanim.i = animation_id;
        overlay->x4_pri = duration;
        overlay->x8_ptr1 = animations[animation_id].unk;
        overlay->x0_timer = 0;
        overlay->xC_loop = 0;
        overlay->x7C_color_enable = overlay->x7C_flag2 = false;
        return true;
    }
    return false;
}

void lb_80014534(void)
{
    lbArchive_80017040(NULL, "LbRb.dat", &lb_804D63C0, "lbRumbleData", 0);
}

void lb_80014574(u8 channel, int id, int rumble_id, int duration)
{
    HSD_PadRumbleAdd(channel, id, duration != 0 ? duration : -2,
                     lb_804D63C0[rumble_id].unk4, lb_804D63C0[rumble_id].unk);
}

void lb_800145C0(u8 slot)
{
    HSD_PadRumbleRemove(slot);
    HSD_PadRumbleOn(slot);
}

void lb_800145F4(void)
{
    int channel;
    for (channel = 0; channel < PAD_MAX_CONTROLLERS; channel++) {
        lb_800145C0(channel);
    }
}
