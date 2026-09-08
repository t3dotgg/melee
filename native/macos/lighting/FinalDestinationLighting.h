// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_FINAL_DESTINATION_LIGHTING_H
#define MELEE_FINAL_DESTINATION_LIGHTING_H

#include <math.h>

/* Gr_Kind_Last is 0x25. GrNLa.dat has a magenta fighter key and a bright
 * neutral ambient. Keep part of the source light while making the key cooler
 * and the shade darker. Original light positions and sky animations remain.
 */
static void melee_final_destination_lighting(MeleeStageLightingProfile* p,
                                             float frame)
{
    /* One slow color cycle per 48 seconds of simulation. */
    float energy = sinf(frame * 0.00218166156F);

    p->ambient_rgb[0] = 96.0F;
    p->ambient_rgb[1] = 112.0F + energy * 3.0F;
    p->ambient_rgb[2] = 150.0F + energy * 5.0F;
    p->key_rgb[0] = 236.0F;
    p->key_rgb[1] = 249.0F;
    p->key_rgb[2] = 255.0F;
    p->fill_rgb[0] = 112.0F;
    p->fill_rgb[1] = 164.0F + energy * 6.0F;
    p->fill_rgb[2] = 245.0F;
    p->light_mix = 0.75F;

    /* The existing 5000..10000 depth fog belongs to the distant sky. Blend
     * its animated color without moving the fog into the fighting area.
     */
    p->fog_rgb[0] = 20.0F;
    p->fog_rgb[1] = 15.0F;
    p->fog_rgb[2] = 48.0F;
    p->fog_mix = 0.25F;
}

/* Map IDs are the GrNLa.dat map_head entries. Change material colors after
 * their original animation. Alpha, texture motion, and vertex colors remain
 * under the original scene controller in grLast_8021B920.
 */
static void melee_final_destination_material(MeleeStageMaterialProfile* p,
                                             unsigned map, float frame)
{
    (void) frame;
    switch (map) {
    case 3:
        /* The platform has a blue diffuse/specular edge material. */
        p->ambient_gain[0] = 0.90F;
        p->ambient_gain[1] = 0.98F;
        p->ambient_gain[2] = 1.08F;
        p->diffuse_gain[0] = 1.04F;
        p->diffuse_gain[1] = 1.24F;
        p->diffuse_gain[2] = 1.30F;
        p->specular_gain[1] = 1.18F;
        p->shininess_gain = 1.22F;
        break;
    case 4:
        /* Milky Way and star layers. Keep white peaks below saturation. */
        p->diffuse_gain[0] = 0.84F;
        p->diffuse_gain[1] = 0.96F;
        break;
    case 5:
        /* Energy lines retain their green and violet animation tracks. */
        p->diffuse_gain[0] = 0.90F;
        p->diffuse_gain[2] = 1.10F;
        break;
    case 6:
        /* Cool the wall layers while retaining their texture detail. */
        p->diffuse_gain[0] = 0.82F;
        p->diffuse_gain[1] = 0.94F;
        break;
    default:
        break;
    }
}

/* The platform's magenta, white, and orange trim is authored unlit geometry,
 * not a missing texture. These exact colors have no material color tracks
 * in GrNLa.dat. The caller limits this palette to opaque, untextured constant
 * materials. Keep the alpha byte and all other material colors unchanged.
 */
static unsigned melee_final_destination_diffuse(unsigned original,
                                                unsigned map)
{
    unsigned color;
    if (map != 3) {
        return original;
    }
    switch (original >> 8) {
    case 0xFF00FF:
        color = 0x3E8ED4;
        break;
    case 0xFFFFFF:
        color = 0x98D0E8;
        break;
    case 0xFF5900:
        color = 0xD68C2C;
        break;
    default:
        return original;
    }
    return (color << 8) | (original & 0xFFU);
}

#endif
