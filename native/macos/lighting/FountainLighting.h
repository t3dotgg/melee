// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_FOUNTAIN_LIGHTING_H
#define MELEE_FOUNTAIN_LIGHTING_H

/* GrIz.dat uses one ambient and two animated point lights. Keep their paths
 * and attenuation. A blue light and a pink light separate the fountain's
 * curved stone, water, and gold trim without replacing their textures. */
static void melee_fountain_lighting(MeleeStageLightingProfile* p, float frame)
{
    (void) frame;
    p->ambient_rgb[0] = 128.0F;
    p->ambient_rgb[1] = 128.0F;
    p->ambient_rgb[2] = 148.0F;
    p->key_rgb[0] = 72.0F;
    p->key_rgb[1] = 106.0F;
    p->key_rgb[2] = 176.0F;
    p->fill_rgb[0] = 160.0F;
    p->fill_rgb[1] = 80.0F;
    p->fill_rgb[2] = 148.0F;
    p->light_mix = 0.80F;
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
        /* Keep the broad column faces below white. Their original material
         * is already pale, so the added blue fill needs no diffuse boost. */
        p->ambient_gain[0] = 0.90F;
        p->ambient_gain[1] = 0.94F;
        p->diffuse_gain[0] = 0.92F;
        p->diffuse_gain[1] = 0.94F;
        p->diffuse_gain[2] = 0.98F;
        p->specular_gain[0] = 0.70F;
        p->specular_gain[1] = 0.82F;
        p->specular_gain[2] = 0.92F;
        p->shininess_gain = 0.80F;
    }
}

#endif
