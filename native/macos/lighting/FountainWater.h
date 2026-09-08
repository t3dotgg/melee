// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_FOUNTAIN_WATER_H
#define MELEE_FOUNTAIN_WATER_H

/* Included only in the generated grizumi chunk. Keep the reflection camera,
 * image descriptor, and preloaded buffer at the same size. The reserve hook
 * runs before the camera and image are created. A live lighting switch takes
 * effect for reflection resolution when the next stage is loaded. */
int melee_stage_lighting_enabled(void);

static int melee_fountain_sharp_water;

static void melee_fountain_reserve_water(CPUState* ctx)
{
    melee_fountain_sharp_water = melee_stage_lighting_enabled();
    if (melee_fountain_sharp_water) {
        ctx->gpr[3] = 320;
        ctx->gpr[4] = 240;
    }
}

static void melee_fountain_water_camera(CPUState* ctx)
{
    u32 descriptor = ctx->gpr[3];
    u16 width = melee_fountain_sharp_water ? 320 : 80;
    u16 height = melee_fountain_sharp_water ? 240 : 60;
    /* HSD_CameraDescPerspective.viewport and .scissor, cobj.h. */
    mem_write16(ctx, descriptor + 0x0A, width);
    mem_write16(ctx, descriptor + 0x0E, height);
    mem_write16(ctx, descriptor + 0x12, width);
    mem_write16(ctx, descriptor + 0x16, height);
}

static void melee_fountain_water_image(CPUState* ctx)
{
    if (melee_fountain_sharp_water) {
        ctx->gpr[4] = 320;
        ctx->gpr[5] = 240;
    }
    /* Retain RGB565. RGBA8 would expose the reflection pass's erase alpha
     * to the water material, which originally samples an opaque image. */
}

#endif
