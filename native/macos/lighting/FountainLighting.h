// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_FOUNTAIN_LIGHTING_H
#define MELEE_FOUNTAIN_LIGHTING_H

/* GrIz.dat uses one ambient and two animated point lights. Keep their paths
 * and attenuation. A blue light and a pink light separate the fountain's
 * curved stone, water, and gold trim without replacing their textures. */
static void melee_fountain_lighting(MeleeStageLightingProfile* p, float frame)
{
    (void) frame;
    p->ambient_rgb[0] = 112.0F;
    p->ambient_rgb[1] = 124.0F;
    p->ambient_rgb[2] = 150.0F;
    p->key_rgb[0] = 90.0F;
    p->key_rgb[1] = 138.0F;
    p->key_rgb[2] = 190.0F;
    p->fill_rgb[0] = 186.0F;
    p->fill_rgb[1] = 100.0F;
    p->fill_rgb[2] = 164.0F;
}

static void melee_fountain_material(MeleeStageMaterialProfile* p, unsigned map,
                                    float frame)
{
    (void) frame;
    /* Map 1 contains the sky, aurora, and trees. Map 3 contains the fountain
     * and water. Map 2 is the reflected platform geometry. Leave map 4's
     * animated spray alone so its alpha and white highlights remain intact. */
    if (map == 1) {
        p->ambient_gain[0] = 0.84F;
        p->ambient_gain[1] = 0.92F;
        p->diffuse_gain[0] = 0.84F;
        p->diffuse_gain[1] = 0.94F;
    } else if (map == 2 || map == 3) {
        p->ambient_gain[0] = 0.92F;
        p->ambient_gain[2] = 1.08F;
        p->diffuse_gain[0] = 0.96F;
        p->diffuse_gain[1] = 1.02F;
        p->diffuse_gain[2] = 1.04F;
        p->specular_gain[0] = 0.78F;
        p->specular_gain[1] = 0.90F;
        p->shininess_gain = 0.80F;
    }
}

#endif
