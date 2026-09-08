// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_BATTLEFIELD_LIGHTING_H
#define MELEE_BATTLEFIELD_LIGHTING_H

/* Gr_Kind_Battle is 0x24, while the match stage ID is 0x1F. GrNBa.dat has
 * one stage directional light and two fighter directional lights in map_plit.
 * A low warm key and a cool opposing fill keep the fighter shapes readable.
 */
static void melee_battlefield_lighting(MeleeStageLightingProfile* p,
                                       float frame)
{
    (void) frame;
    p->ambient_rgb[0] = 88.0F;
    p->ambient_rgb[1] = 101.0F;
    p->ambient_rgb[2] = 130.0F;
    p->key_rgb[0] = 238.0F;
    p->key_rgb[1] = 184.0F;
    p->key_rgb[2] = 124.0F;
    p->fill_rgb[0] = 82.0F;
    p->fill_rgb[1] = 116.0F;
    p->fill_rgb[2] = 168.0F;
    p->key_direction[0] = -0.85F;
    p->key_direction[1] = 0.65F;
    p->key_direction[2] = 0.90F;
    p->fill_direction[0] = 0.85F;
    p->fill_direction[1] = 0.30F;
    p->fill_direction[2] = 0.55F;
}

static void melee_battlefield_material(MeleeStageMaterialProfile* p,
                                       unsigned map, float frame)
{
    (void) frame;
    switch (map) {
    case 6:
        /* grBattle_OnInit always loads map 6, the visible platforms. Most
         * of its meshes use vertex colors without normals. Warm the existing
         * material trim without enabling lights on those unlit meshes.
         */
        p->diffuse_gain[0] = 1.00F;
        p->diffuse_gain[1] = 0.92F;
        p->diffuse_gain[2] = 0.74F;
        break;
    case 1:
    case 2:
    case 4:
        /* grBattle_BG_Callback2 cycles these three backgrounds. Their lit
         * cloud material has blue ambient color. Reduce the white highlights
         * so the clouds remain behind the warm platform and fighters.
         */
        p->ambient_gain[0] = 0.90F;
        p->ambient_gain[1] = 1.00F;
        p->ambient_gain[2] = 1.00F;
        p->diffuse_gain[0] = 0.78F;
        p->diffuse_gain[1] = 0.87F;
        p->diffuse_gain[2] = 1.00F;
        break;
    }
}

#endif
