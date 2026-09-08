// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_FOUNTAIN_WATER_H
#define MELEE_FOUNTAIN_WATER_H

/* Included only in the generated grizumi chunk. Old save states and cached
 * stages can retain the original buffer. Check the guest allocation before
 * enlarging its camera or image. A fresh preload enables sharp reflections. */
int melee_stage_lighting_enabled(void);

#define MELEE_FOUNTAIN_CAMERA_DESC 0x803E0F34U

static void melee_fountain_reserve_water(CPUState* ctx)
{
    if (melee_stage_lighting_enabled()) {
        ctx->gpr[3] = 320;
        ctx->gpr[4] = 240;
    }
}

static int melee_fountain_can_render_sharp_water(CPUState* ctx)
{
    unsigned index;
    if (!melee_stage_lighting_enabled()) {
        return 0;
    }
    /* preloadCache.entries, PreloadEntry, and lbDvd_GetPreloadedArchive.
     * lbDvd_80017740 retains an existing entry even when a new reservation
     * asks for more bytes. The entry's size is the actual allocation limit. */
    for (index = 0; index < 80; index++) {
        u32 entry = 0x80432124U + index * 0x1CU;
        unsigned score = mem_read16(ctx, entry + 8U);
        if (mem_read8(ctx, entry) != 0 && score > 0 && score < 0x8000U &&
            mem_read16(ctx, entry + 6U) == 2001)
        {
            return mem_read32(ctx, entry + 0xCU) >= 320U * 240U * 2U;
        }
    }
    /* With no matching entry, lb_800121FC allocates a new image buffer using
     * the requested dimensions. This also supports direct scene restarts. */
    return 1;
}

static void melee_fountain_water_camera(CPUState* ctx)
{
    u32 descriptor = ctx->gpr[3];
    int sharp = melee_fountain_can_render_sharp_water(ctx);
    u16 width = sharp ? 320 : 80;
    u16 height = sharp ? 240 : 60;
    /* HSD_CameraDescPerspective.viewport and .scissor, cobj.h. */
    mem_write16(ctx, descriptor + 0x0A, width);
    mem_write16(ctx, descriptor + 0x0E, height);
    mem_write16(ctx, descriptor + 0x12, width);
    mem_write16(ctx, descriptor + 0x16, height);
}

static void melee_fountain_water_image(CPUState* ctx)
{
    /* The guest descriptor carries the camera choice through save/load and
     * through a settings change between camera and image construction. */
    if (mem_read16(ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x0AU) == 320 &&
        mem_read16(ctx, MELEE_FOUNTAIN_CAMERA_DESC + 0x0EU) == 240)
    {
        ctx->gpr[4] = 320;
        ctx->gpr[5] = 240;
    }
    /* Retain RGB565. RGBA8 would expose the reflection pass's erase alpha
     * to the water material, which originally samples an opaque image. */
}

#endif
