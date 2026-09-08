// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELEE_HIGH_REFRESH_H
#define MELEE_HIGH_REFRESH_H

#include <errno.h>
#include <stdlib.h>
#include <time.h>

/* Included only in the generated chunk containing gm_801A4D34. Guest addresses
 * and offsets are from the verified USA v1.02 symbols, HSD_JObj, and HSD_GObj.
 * The original executable, animation clocks, and collision updates stay at 60.
 */
#define MELEE_POSE_CAPACITY 8192
#define MELEE_POSE_LIMIT 4096

typedef struct MeleePose {
    u32 address;
    u32 owner;
    u32 identity[5];
    u32 frame;
    u32 saved[10];
    u32 saved_matrix[12];
    u32 saved_auxiliary[12];
    u32 auxiliary_address;
    float previous[10];
    float predicted[10];
    u32 flags_address;
    u32 dirty_mask;
    unsigned count;
    unsigned rotation;
} MeleePose;

static MeleePose melee_poses[MELEE_POSE_CAPACITY];
static MeleePose* melee_active_poses[MELEE_POSE_LIMIT];
static unsigned melee_pose_count;
static u32 melee_pose_frame;
static int melee_extra_render;
static int melee_refresh_mode;
static u64 melee_next_present_ns;
static unsigned melee_present_remainder;

static int melee_refresh_enabled(void)
{
    if (melee_refresh_mode == 0) {
        const char* value = getenv("MELEE_RENDER_FPS");
        melee_refresh_mode =
            value == NULL || strcmp(value, "120") == 0 ? 1 : -1;
    }
    return melee_refresh_mode > 0;
}

static u64 melee_monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64) now.tv_sec * 1000000000ULL + (u64) now.tv_nsec;
}

static void melee_refresh_reset(void)
{
    memset(melee_poses, 0, sizeof(melee_poses));
    melee_pose_count = 0;
    melee_pose_frame = 0;
    melee_extra_render = 0;
    melee_next_present_ns = 0;
    melee_present_remainder = 0;
}

/* Space actual GX renders by 8.333 ms. Do not queue a burst after a slow
 * frame. Rational deadlines retain the fractional third of a nanosecond at 120
 * Hz.
 */
static void melee_refresh_pace(void)
{
    u64 now;
    if (!melee_refresh_enabled()) {
        return;
    }
    now = melee_monotonic_ns();
    if (melee_next_present_ns == 0 ||
        now > melee_next_present_ns + 16666667ULL)
    {
        melee_next_present_ns = now;
        melee_present_remainder = 0;
    }
    while (now < melee_next_present_ns) {
        u64 delay = melee_next_present_ns - now;
        struct timespec wait = { (time_t) (delay / 1000000000ULL),
                                 (long) (delay % 1000000000ULL) };
        while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {
        }
        now = melee_monotonic_ns();
    }
    melee_next_present_ns += 8333333ULL;
    melee_present_remainder++;
    if (melee_present_remainder == 3) {
        melee_next_present_ns++;
        melee_present_remainder = 0;
    }
}

static int melee_ram(u32 address, unsigned bytes)
{
    return address >= 0x80003100U && address <= 0x81800000U - bytes &&
           (address & 3U) == 0;
}

static float melee_float(u32 bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static u32 melee_bits(float value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* Predict half a frame without blending across teleports or sudden pose cuts.
 * Euler angles use the short arc. Quaternion joints and custom matrices skip
 * prediction because a linear component update does not preserve their basis.
 */
static int melee_predict(float* result, const float* current,
                         const float* previous, unsigned count,
                         unsigned rotation)
{
    unsigned i;
    for (i = 0; i < count; i++) {
        float delta = current[i] - previous[i];
        float limit = 20.0F;
        /* Euler rotation.w is unused. Keep scale unchanged because crossing
         * unit scale allocates or frees the joint's derived scale vector. */
        if (rotation && i >= 3 && i < 7) {
            result[i] = current[i];
            continue;
        }
        if (!isfinite(current[i]) || !isfinite(previous[i])) {
            return 0;
        }
        if (rotation && i < 3) {
            delta = remainderf(delta, 6.283185307179586F);
            limit = 1.2F;
        }
        if (fabsf(delta) > limit) {
            return 0;
        }
        result[i] = current[i] + delta * 0.5F;
    }
    return 1;
}

static MeleePose* melee_pose_slot(u32 address)
{
    unsigned slot = ((address >> 2) * 2654435761U) & (MELEE_POSE_CAPACITY - 1);
    unsigned i;
    for (i = 0; i < MELEE_POSE_CAPACITY; i++) {
        MeleePose* pose = &melee_poses[slot];
        if (pose->address == address || pose->frame != melee_pose_frame) {
            return pose;
        }
        slot = (slot + 1) & (MELEE_POSE_CAPACITY - 1);
    }
    return NULL;
}

static void melee_capture_pose(CPUState* ctx, u32 address, u32 owner,
                               const u32 identity[5], unsigned count,
                               unsigned rotation, u32 flags_address,
                               u32 dirty_mask)
{
    MeleePose* pose;
    float current[10];
    u32 saved[10];
    unsigned i;
    int continuous;
    if (melee_pose_count == MELEE_POSE_LIMIT || !melee_ram(address, count * 4))
    {
        return;
    }
    pose = melee_pose_slot(address);
    if (pose == NULL ||
        (pose->address == address && pose->frame == melee_pose_frame))
    {
        return;
    }
    for (i = 0; i < count; i++) {
        saved[i] = mem_read32(ctx, address + i * 4);
        current[i] = melee_float(saved[i]);
    }
    continuous = pose->address == address && pose->owner == owner &&
                 pose->frame + 1 == melee_pose_frame && pose->count == count &&
                 memcmp(pose->identity, identity, sizeof(pose->identity)) == 0;
    if (!continuous || !melee_predict(pose->predicted, current, pose->previous,
                                      count, rotation))
    {
        memcpy(pose->predicted, current, count * sizeof(float));
    }
    pose->address = address;
    pose->owner = owner;
    pose->frame = melee_pose_frame;
    pose->count = count;
    pose->rotation = rotation;
    pose->flags_address = flags_address;
    pose->dirty_mask = dirty_mask;
    memcpy(pose->identity, identity, sizeof(pose->identity));
    memcpy(pose->saved, saved, count * sizeof(u32));
    memcpy(pose->previous, current, count * sizeof(float));
    {
        u32 object = rotation ? address - 0x1CU : flags_address - 8;
        u32 matrix = object + (rotation ? 0x44U : 0x54U);
        u32 auxiliary = mem_read32(ctx, object + (rotation ? 0x74U : 0x88U));
        unsigned auxiliary_count = rotation ? 3 : 12;
        for (i = 0; i < 12; i++) {
            pose->saved_matrix[i] = mem_read32(ctx, matrix + i * 4);
        }
        pose->auxiliary_address = 0;
        if (melee_ram(auxiliary, auxiliary_count * 4)) {
            pose->auxiliary_address = auxiliary;
            for (i = 0; i < auxiliary_count; i++) {
                pose->saved_auxiliary[i] = mem_read32(ctx, auxiliary + i * 4);
            }
        }
    }
    melee_active_poses[melee_pose_count++] = pose;
}

static void melee_capture_joints(CPUState* ctx, u32 root, u32 owner,
                                 u32 action, u32 spawn)
{
    u32 pending[MELEE_POSE_LIMIT];
    unsigned size = 0;
    unsigned visited = 0;
    pending[size++] = root;
    while (size != 0 && visited++ < MELEE_POSE_LIMIT) {
        u32 joint = pending[--size];
        u32 flags;
        u32 identity[5];
        u32 child;
        u32 next;
        if (!melee_ram(joint, 0x88)) {
            continue;
        }
        flags = mem_read32(ctx, joint + 0x14);
        child = mem_read32(ctx, joint + 0x10);
        next = mem_read32(ctx, joint + 8);
        if (child != 0 && size < MELEE_POSE_LIMIT) {
            pending[size++] = child;
        }
        if (next != 0 && size < MELEE_POSE_LIMIT) {
            pending[size++] = next;
        }
        if ((flags & ((1U << 17) | (1U << 23) | (1U << 25))) != 0) {
            continue;
        }
        identity[0] = mem_read32(ctx, joint);
        identity[1] = mem_read32(ctx, joint + 0x18);
        identity[2] = mem_read32(ctx, joint + 0x7C);
        identity[3] = action;
        identity[4] = spawn;
        melee_capture_pose(ctx, joint + 0x1C, owner, identity, 10, 1,
                           joint + 0x14, 1U << 6);
    }
}

static void melee_capture_scene(CPUState* ctx)
{
    u32 lists = mem_read32(ctx, 0x804D782CU);
    unsigned links = mem_read8(ctx, 0x804CE380U) + 1U;
    u32 frame = mem_read32(ctx, 0x80479D58U);
    unsigned link;
    unsigned visited = 0;
    u32 joint_kind = mem_read8(ctx, 0x804D7849U);
    u32 camera_kind = mem_read8(ctx, 0x804D784BU);
    if (frame != melee_pose_frame + 1 || frame == 0) {
        memset(melee_poses, 0, sizeof(melee_poses));
    }
    melee_pose_frame = frame;
    melee_pose_count = 0;
    if (links > 64 || !melee_ram(lists, links * 4)) {
        return;
    }
    for (link = 0; link < links; link++) {
        u32 object = mem_read32(ctx, lists + link * 4);
        while (melee_ram(object, 0x38) && visited++ < MELEE_POSE_LIMIT) {
            u32 kind = mem_read8(ctx, object + 6);
            u32 data = mem_read32(ctx, object + 0x28);
            if (kind == joint_kind) {
                u32 fighter = mem_read32(ctx, object + 0x2C);
                u32 action = 0;
                u32 spawn = 0;
                if (mem_read16(ctx, object) == 4 && melee_ram(fighter, 0x18)) {
                    action = mem_read32(ctx, fighter + 0x10);
                    spawn = mem_read32(ctx, fighter + 8);
                }
                melee_capture_joints(ctx, data, object, action, spawn);
            } else if (kind == camera_kind && melee_ram(data, 0x8C)) {
                unsigned which;
                for (which = 0; which < 2; which++) {
                    u32 world = mem_read32(ctx, data + 0x24 + which * 4);
                    u32 identity[5] = { data, which, 0, 0, 0 };
                    if (melee_ram(world, 0x20)) {
                        identity[2] = mem_read32(ctx, world);
                        identity[3] = mem_read32(ctx, world + 0x18);
                        identity[4] = mem_read8(ctx, data + 0x50);
                        melee_capture_pose(ctx, world + 0xC, object, identity,
                                           3, 0, data + 8, 0xC0000000U);
                    }
                }
            }
            object = mem_read32(ctx, object + 8);
        }
    }
}

/* Save only visual transforms. Restore their exact bits before the next game
 * update and mark the derived matrices dirty. Physics never sees predicted
 * poses.
 */
static void melee_write_poses(CPUState* ctx, int predicted)
{
    unsigned index;
    for (index = 0; index < melee_pose_count; index++) {
        MeleePose* pose = melee_active_poses[index];
        unsigned i;
        /* A render callback can remove a visual object. Do not restore into
         * storage that the object allocator has already reused. */
        u32 object = pose->address - (pose->rotation ? 0x1CU : 0xCU);
        u32 identity = pose->rotation ? pose->identity[0] : pose->identity[2];
        if (!melee_ram(pose->owner, 0x38) ||
            mem_read32(ctx, object) != identity)
        {
            continue;
        }
        if (pose->rotation &&
            (mem_read32(ctx, object + 0x18) != pose->identity[1] ||
             mem_read32(ctx, object + 0x7C) != pose->identity[2]))
        {
            continue;
        }
        for (i = 0; i < pose->count; i++) {
            mem_write32(ctx, pose->address + i * 4,
                        predicted ? melee_bits(pose->predicted[i])
                                  : pose->saved[i]);
        }
        if (!predicted) {
            u32 matrix_object =
                pose->rotation ? object : pose->flags_address - 8;
            u32 matrix = matrix_object + (pose->rotation ? 0x44U : 0x54U);
            u32 auxiliary = mem_read32(
                ctx, matrix_object + (pose->rotation ? 0x74U : 0x88U));
            unsigned auxiliary_count = pose->rotation ? 3 : 12;
            for (i = 0; i < 12; i++) {
                mem_write32(ctx, matrix + i * 4, pose->saved_matrix[i]);
            }
            if (auxiliary != 0 && auxiliary == pose->auxiliary_address) {
                for (i = 0; i < auxiliary_count; i++) {
                    mem_write32(ctx, auxiliary + i * 4,
                                pose->saved_auxiliary[i]);
                }
            }
        }
        mem_write32(ctx, pose->flags_address,
                    mem_read32(ctx, pose->flags_address) | pose->dirty_mask);
    }
}

static int melee_refresh_finish(CPUState* ctx)
{
    if (!melee_refresh_enabled()) {
        return 0;
    }
    if (melee_extra_render) {
        melee_write_poses(ctx, 0);
        melee_extra_render = 0;
        return 0;
    }
    melee_capture_scene(ctx);
    melee_write_poses(ctx, 1);
    melee_extra_render = 1;
    return 1;
}

#endif
