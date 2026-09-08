// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_STAGE_LIGHTING_H
#define MELEE_STAGE_LIGHTING_H

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Included in one translated game chunk. All addresses and object layouts are
 * from the verified US v1.02 executable and the HSD headers in this
 * repository. Only draw-time colors and light directions change. The
 * transaction restores the edited source fields before simulation or a state
 * save resumes. HSD rebuilds its derived hardware light data on the next
 * camera setup.
 */
typedef struct MeleeStageLightingProfile {
    float ambient_rgb[3];
    float key_rgb[3];
    float fill_rgb[3];
    float key_direction[3];
    float fill_direction[3];
    float light_mix;
    float fog_rgb[3];
    float fog_mix;
} MeleeStageLightingProfile;

typedef struct MeleeStageMaterialProfile {
    float ambient_gain[3];
    float diffuse_gain[3];
    float specular_gain[3];
    float shininess_gain;
} MeleeStageMaterialProfile;

#include "BattlefieldLighting.h"
#include "FinalDestinationLighting.h"
#include "FountainLighting.h"

#define MELEE_STAGE_INFO 0x8049E6C8U
#define MELEE_STAGE_MAPS (MELEE_STAGE_INFO + 0x180U)
#define MELEE_STAGE_FRAME 0x80479D58U
#define MELEE_STAGE_WRITES 8192U
#define MELEE_STAGE_HASH_SIZE 16384U
#define MELEE_STAGE_WALK_LIMIT 4096U

typedef struct MeleeStageWrite {
    u32 address;
    u32 original;
    u32 applied;
    u32 owner;
    u32 identity;
} MeleeStageWrite;

static MeleeStageWrite melee_stage_writes[MELEE_STAGE_WRITES];
static unsigned melee_stage_write_slots[MELEE_STAGE_HASH_SIZE];
static unsigned melee_stage_write_count;
static unsigned melee_stage_light_count;
static unsigned melee_stage_material_count;
static int melee_stage_active;
static u32 melee_stage_kind;
static u32 melee_stage_frame;
static u32 melee_stage_anchor;
static unsigned melee_stage_anchor_map;
static MeleeStageLightingProfile melee_stage_profile;

/* The app provides a live switch. Standalone translated modules use the same
 * environment setting, which also makes the comparison test reproducible.
 */
int melee_stage_lighting_enabled(void)
{
    typedef int (*MeleeLightingSetting)(void);
    static MeleeLightingSetting setting;
    static int checked;
    if (!checked) {
        void* symbol = dlsym(RTLD_DEFAULT, "MeleeStageLightingEnabled");
        memcpy(&setting, &symbol, sizeof(setting));
        checked = 1;
    }
    if (setting != NULL) {
        return setting() != 0;
    }
    {
        const char* value = getenv("MELEE_STAGE_LIGHTING");
        return value == NULL || strcmp(value, "0") != 0;
    }
}

static int melee_stage_ram(u32 address, unsigned bytes)
{
    return bytes <= 0x1800000U && address >= 0x80003100U &&
           address <= 0x81800000U - bytes && (address & 3U) == 0;
}

static float melee_stage_float(u32 bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 melee_stage_bits(float value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static unsigned melee_stage_byte(float value)
{
    if (!isfinite(value) || value <= 0.0F) {
        return 0;
    }
    return value >= 255.0F ? 255U : (unsigned) (value + 0.5F);
}

static u32 melee_stage_color(u32 original, const float rgb[3], float mix)
{
    u32 result = original & 255U;
    unsigned channel;
    for (channel = 0; channel < 3; channel++) {
        unsigned shift = 24U - channel * 8U;
        float source = (float) ((original >> shift) & 255U);
        result |= melee_stage_byte(source + (rgb[channel] - source) * mix)
                  << shift;
    }
    return result;
}

static u32 melee_stage_gain(u32 original, const float gain[3])
{
    u32 result = original & 255U;
    unsigned channel;
    for (channel = 0; channel < 3; channel++) {
        unsigned shift = 24U - channel * 8U;
        result |= melee_stage_byte((float) ((original >> shift) & 255U) *
                                   gain[channel])
                  << shift;
    }
    return result;
}

/* An address is changed at most once per render. Materials and WObjs can be
 * shared by several joints, cameras, or light lists. Repeated visits must not
 * compound their color gains or replace the original snapshot.
 */
static void melee_stage_write(CPUState* ctx, u32 address, u32 value, u32 owner)
{
    unsigned slot;
    unsigned probes;
    u32 original;
    if (!melee_stage_ram(address, 4) || !melee_stage_ram(owner, 4)) {
        return;
    }
    slot = ((address >> 2) * 2654435761U) & (MELEE_STAGE_HASH_SIZE - 1U);
    for (probes = 0; probes < MELEE_STAGE_HASH_SIZE; probes++) {
        unsigned entry = melee_stage_write_slots[slot];
        if (entry == 0) {
            break;
        }
        if (melee_stage_writes[entry - 1U].address == address) {
            return;
        }
        slot = (slot + 1U) & (MELEE_STAGE_HASH_SIZE - 1U);
    }
    original = mem_read32(ctx, address);
    if (original == value || melee_stage_write_count == MELEE_STAGE_WRITES ||
        probes == MELEE_STAGE_HASH_SIZE)
    {
        return;
    }
    {
        MeleeStageWrite* write = &melee_stage_writes[melee_stage_write_count];
        write->address = address;
        write->original = original;
        write->applied = value;
        write->owner = owner;
        write->identity = mem_read32(ctx, owner);
    }
    melee_stage_write_slots[slot] = ++melee_stage_write_count;
    mem_write32(ctx, address, value);
}

static int melee_stage_same_scene(CPUState* ctx)
{
    return mem_read32(ctx, MELEE_STAGE_INFO + 0x88U) == melee_stage_kind &&
           mem_read32(ctx, MELEE_STAGE_FRAME) == melee_stage_frame &&
           mem_read32(ctx, MELEE_STAGE_MAPS + melee_stage_anchor_map * 4U) ==
               melee_stage_anchor;
}

void melee_stage_lighting_finish(CPUState* ctx)
{
    unsigned i;
    if (melee_stage_active && melee_stage_same_scene(ctx)) {
        for (i = 0; i < melee_stage_write_count; i++) {
            const MeleeStageWrite* write = &melee_stage_writes[i];
            /* Rendering can remove an object or rewrite an animated value.
             * Restore only our own unchanged writes to a surviving object.
             */
            if (mem_read32(ctx, write->owner) == write->identity &&
                mem_read32(ctx, write->address) == write->applied)
            {
                mem_write32(ctx, write->address, write->original);
            }
        }
    }
    melee_stage_active = 0;
    melee_stage_write_count = 0;
}

void melee_stage_lighting_prepare_state(CPUState* ctx, u32 loading)
{
    melee_stage_lighting_finish(ctx);
    (void) loading;
}

static void melee_stage_material_profile(MeleeStageMaterialProfile* profile,
                                         unsigned map)
{
    unsigned channel;
    for (channel = 0; channel < 3; channel++) {
        profile->ambient_gain[channel] = 1.0F;
        profile->diffuse_gain[channel] = 1.0F;
        profile->specular_gain[channel] = 1.0F;
    }
    profile->shininess_gain = 1.0F;
    switch (melee_stage_kind) {
    case 0x0C:
        melee_fountain_material(profile, map, (float) melee_stage_frame);
        break;
    case 0x24:
        melee_battlefield_material(profile, map, (float) melee_stage_frame);
        break;
    case 0x25:
        melee_final_destination_material(profile, map,
                                         (float) melee_stage_frame);
        break;
    }
}

static void melee_stage_material(CPUState* ctx, u32 mobj, unsigned map,
                                 const MeleeStageMaterialProfile* profile)
{
    u32 material;
    u32 original_diffuse;
    u32 diffuse;
    float shininess;
    if (!melee_stage_ram(mobj, 0x24)) {
        return;
    }
    material = mem_read32(ctx, mobj + 0xCU);
    if (!melee_stage_ram(material, 0x14)) {
        return;
    }
    melee_stage_material_count++;
    melee_stage_write(
        ctx, material,
        melee_stage_gain(mem_read32(ctx, material), profile->ambient_gain),
        mobj);
    original_diffuse = mem_read32(ctx, material + 4U);
    diffuse = original_diffuse;
    /* Final Destination has authored, untextured magenta trim. Give only
     * those opaque constant materials the new palette. Vertex colors,
     * textures, lit materials, and translucent effects keep their own colors.
     * The optional toon flag does not change this constant-material test.
     */
    if (melee_stage_kind == 0x25 && map == 3 &&
        (mem_read32(ctx, mobj + 4U) & 0x60000FFFU) == 1U)
    {
        diffuse = melee_final_destination_diffuse(original_diffuse, map);
    }
    if (diffuse == original_diffuse) {
        diffuse = melee_stage_gain(original_diffuse, profile->diffuse_gain);
    }
    melee_stage_write(ctx, material + 4U, diffuse, mobj);
    melee_stage_write(ctx, material + 8U,
                      melee_stage_gain(mem_read32(ctx, material + 8U),
                                       profile->specular_gain),
                      mobj);
    shininess = melee_stage_float(mem_read32(ctx, material + 0x10U));
    if (isfinite(shininess) && shininess >= 0.0F &&
        profile->shininess_gain != 1.0F)
    {
        shininess = fminf(128.0F, shininess * profile->shininess_gain);
        melee_stage_write(ctx, material + 0x10U, melee_stage_bits(shininess),
                          mobj);
    }
}

/* The joint union also holds particles and splines. Only its DObj form has a
 * material list. No mesh data, normals, render flags, or alpha values change.
 */
static void melee_stage_materials(CPUState* ctx, u32 root, unsigned map)
{
    MeleeStageMaterialProfile profile;
    u32 pending[MELEE_STAGE_WALK_LIMIT];
    unsigned size = 0;
    unsigned visited = 0;
    melee_stage_material_profile(&profile, map);
    pending[size++] = root;
    while (size != 0 && visited++ < MELEE_STAGE_WALK_LIMIT) {
        u32 joint = pending[--size];
        u32 child;
        u32 next;
        u32 flags;
        if (!melee_stage_ram(joint, 0x88)) {
            continue;
        }
        child = mem_read32(ctx, joint + 0x10U);
        next = mem_read32(ctx, joint + 8U);
        if (child != 0 && size < MELEE_STAGE_WALK_LIMIT) {
            pending[size++] = child;
        }
        if (next != 0 && size < MELEE_STAGE_WALK_LIMIT) {
            pending[size++] = next;
        }
        flags = mem_read32(ctx, joint + 0x14U);
        if ((flags & ((1U << 5) | (1U << 14))) == 0) {
            u32 dobj = mem_read32(ctx, joint + 0x18U);
            unsigned draws = 0;
            while (melee_stage_ram(dobj, 0x18) && draws++ < 512) {
                melee_stage_material(ctx, mem_read32(ctx, dobj + 8U), map,
                                     &profile);
                dobj = mem_read32(ctx, dobj + 4U);
            }
        }
    }
}

static int melee_stage_map_object(CPUState* ctx, u32 gobj, unsigned map)
{
    u32 ground;
    if (!melee_stage_ram(gobj, 0x38) ||
        mem_read8(ctx, gobj + 6U) != mem_read8(ctx, 0x804D7849U))
    {
        return 0;
    }
    ground = mem_read32(ctx, gobj + 0x2CU);
    return melee_stage_ram(ground, 0x18) &&
           mem_read32(ctx, ground + 0x14U) == map &&
           mem_read32(ctx, ground + 4U) == gobj;
}

void melee_stage_lighting_begin(CPUState* ctx)
{
    unsigned map;
    unsigned channel;
    melee_stage_lighting_finish(ctx);
    if (!melee_stage_lighting_enabled()) {
        return;
    }
    melee_stage_kind = mem_read32(ctx, MELEE_STAGE_INFO + 0x88U);
    if (melee_stage_kind != 0x0C && melee_stage_kind != 0x24 &&
        melee_stage_kind != 0x25)
    {
        return;
    }
    /* Stage destruction clears these slots. Check the live Ground back
     * pointer as well, so stale stage IDs cannot relight menus or results.
     */
    for (map = 0; map < 64; map++) {
        u32 gobj = mem_read32(ctx, MELEE_STAGE_MAPS + map * 4U);
        if (melee_stage_map_object(ctx, gobj, map)) {
            melee_stage_anchor = gobj;
            melee_stage_anchor_map = map;
            break;
        }
    }
    if (map == 64) {
        return;
    }
    melee_stage_frame = mem_read32(ctx, MELEE_STAGE_FRAME);
    melee_stage_active = 1;
    melee_stage_light_count = 0;
    melee_stage_material_count = 0;
    memset(melee_stage_write_slots, 0, sizeof(melee_stage_write_slots));
    memset(&melee_stage_profile, 0, sizeof(melee_stage_profile));
    for (channel = 0; channel < 3; channel++) {
        melee_stage_profile.ambient_rgb[channel] = 128.0F;
        melee_stage_profile.key_rgb[channel] = 255.0F;
        melee_stage_profile.fill_rgb[channel] = 255.0F;
    }
    melee_stage_profile.light_mix = 1.0F;
    switch (melee_stage_kind) {
    case 0x0C:
        melee_fountain_lighting(&melee_stage_profile,
                                (float) melee_stage_frame);
        break;
    case 0x24:
        melee_battlefield_lighting(&melee_stage_profile,
                                   (float) melee_stage_frame);
        break;
    case 0x25:
        melee_final_destination_lighting(&melee_stage_profile,
                                         (float) melee_stage_frame);
        break;
    }
    for (map = 0; map < 64; map++) {
        u32 gobj = mem_read32(ctx, MELEE_STAGE_MAPS + map * 4U);
        if (melee_stage_map_object(ctx, gobj, map)) {
            melee_stage_materials(ctx, mem_read32(ctx, gobj + 0x28U), map);
        }
    }
    if (melee_stage_profile.fog_mix != 0.0F) {
        u32 gobj = mem_read32(ctx, MELEE_STAGE_INFO + 0x12CU);
        if (melee_stage_ram(gobj, 0x38)) {
            u32 fog = mem_read32(ctx, gobj + 0x28U);
            if (melee_stage_ram(fog, 0x20)) {
                melee_stage_write(
                    ctx, fog + 0x18U,
                    melee_stage_color(mem_read32(ctx, fog + 0x18U),
                                      melee_stage_profile.fog_rgb,
                                      melee_stage_profile.fog_mix),
                    fog);
            }
        }
    }
}

/* Called before the original stage, fighter, and map-camera light callbacks.
 * HSD_LObjSetupInit then uploads these real light colors and positions. Keep
 * them until the draw ends because ambient and specular setup read LObjs
 * later.
 */
void melee_stage_lighting_lights(CPUState* ctx, u32 gobj)
{
    u32 light;
    unsigned visited = 0;
    unsigned directional = 0;
    if (!melee_stage_active || !melee_stage_same_scene(ctx) ||
        !melee_stage_ram(gobj, 0x38) ||
        mem_read8(ctx, gobj + 6U) != mem_read8(ctx, 0x804D784AU))
    {
        return;
    }
    light = mem_read32(ctx, gobj + 0x28U);
    while (melee_stage_ram(light, 0xD4) && visited++ < 32) {
        unsigned flags = mem_read16(ctx, light + 8U);
        unsigned type = flags & 3U;
        if ((flags & 0x20U) == 0) {
            const float* rgb = melee_stage_profile.ambient_rgb;
            const float* direction = NULL;
            if (type != 0) {
                rgb = directional == 0 ? melee_stage_profile.key_rgb
                                       : melee_stage_profile.fill_rgb;
                direction = directional == 0
                                ? melee_stage_profile.key_direction
                                : melee_stage_profile.fill_direction;
                directional++;
            }
            melee_stage_light_count++;
            melee_stage_write(ctx, light + 0x10U,
                              melee_stage_color(mem_read32(ctx, light + 0x10U),
                                                rgb,
                                                melee_stage_profile.light_mix),
                              light);
            /* Point and spot positions are world coordinates. A direction
             * override applies only to an infinite light's WObj vector.
             */
            if (type == 1 && direction != NULL &&
                direction[0] * direction[0] + direction[1] * direction[1] +
                        direction[2] * direction[2] >
                    0.0001F)
            {
                u32 position = mem_read32(ctx, light + 0x18U);
                if (melee_stage_ram(position, 0x20)) {
                    unsigned axis;
                    melee_stage_write(
                        ctx, position + 8U,
                        (mem_read32(ctx, position + 8U) | 2U) & ~1U, position);
                    for (axis = 0; axis < 3; axis++) {
                        melee_stage_write(ctx, position + 0xCU + axis * 4U,
                                          melee_stage_bits(direction[axis]),
                                          position);
                    }
                }
            }
        }
        light = mem_read32(ctx, light + 0xCU);
    }
}

#endif
