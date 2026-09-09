#ifndef MELEE_NATIVE_COMMAND_H
#define MELEE_NATIVE_COMMAND_H

#include <stdint.h>
#include <stdlib.h>

/* Commands stay in the archive's big-endian, four-byte word format. */
void* native_archive_command_target(const void* command_word);

static inline uint32_t native_command_u32(const void* word)
{
    const uint8_t* bytes = word;
    return (uint32_t) bytes[0] << 24 | (uint32_t) bytes[1] << 16 |
           (uint32_t) bytes[2] << 8 | bytes[3];
}

static inline uint16_t native_command_u16(const void* word, unsigned index)
{
    const uint8_t* bytes = (const uint8_t*) word + index * 2;
    return (uint16_t) ((unsigned) bytes[0] << 8 | bytes[1]);
}

static inline void native_command_store_u32(void* word, uint32_t value)
{
    uint8_t* bytes = word;
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

static inline uint32_t native_command_unsigned(const void* word,
                                               unsigned shift, unsigned bits)
{
    return (native_command_u32(word) >> shift) & (UINT32_MAX >> (32 - bits));
}

static inline int32_t native_command_signed(const void* word, unsigned shift,
                                            unsigned bits)
{
    uint32_t value = native_command_unsigned(word, shift, bits);
    uint32_t sign = UINT32_C(1) << (bits - 1);
    return (int32_t) ((value ^ sign) - sign);
}

#define NATIVE_CMD_READ(pointer, reader, shift, bits)                         \
    reader(pointer, shift, bits)
#define NATIVE_CMD_EXPAND(pointer, layout) NATIVE_CMD_READ(pointer, layout)
#define CMD_FIELD(pointer, kind, field)                                       \
    NATIVE_CMD_EXPAND(pointer, NATIVE_CMD_LAYOUT_##kind##_##field)
#define CMD_U16(pointer, index) native_command_u16(pointer, index)
#define CMD_S16(pointer, index) ((int16_t) native_command_u16(pointer, index))
#define CMD_U32(pointer) native_command_u32(pointer)

/* Field positions follow the original command structs in melee/lb/types.h.
 * Signed fields keep their encoded width before conversion to host integers.
 */
#define NATIVE_CMD_LAYOUT_Command_00_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_Command_02_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_Command_03_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_Command_09_id native_command_unsigned, 26, 6
#define NATIVE_CMD_LAYOUT_Command_09_param_1 native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_Command_09_param_2 native_command_unsigned, 0, 18
#define NATIVE_CMD_LAYOUT_create_hitbox_0_bone native_command_unsigned, 11, 8
#define NATIVE_CMD_LAYOUT_create_hitbox_0_damage native_command_unsigned, 0, 10
#define NATIVE_CMD_LAYOUT_create_hitbox_0_hit_group                           \
    native_command_unsigned, 20, 3
#define NATIVE_CMD_LAYOUT_create_hitbox_0_id native_command_unsigned, 23, 3
#define NATIVE_CMD_LAYOUT_create_hitbox_0_use_common_bone_ids                 \
    native_command_unsigned, 10, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_1_size native_command_unsigned, 16, 16
#define NATIVE_CMD_LAYOUT_create_hitbox_1_z_offset native_command_signed, 0, 16
#define NATIVE_CMD_LAYOUT_create_hitbox_2_x_offset native_command_signed, 0, 16
#define NATIVE_CMD_LAYOUT_create_hitbox_2_y_offset                            \
    native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_create_hitbox_3_angle native_command_unsigned, 23, 9
#define NATIVE_CMD_LAYOUT_create_hitbox_3_clank native_command_unsigned, 1, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_3_ignore_fighter_scale                \
    native_command_unsigned, 2, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_3_item_hit_interaction                \
    native_command_unsigned, 4, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_3_knockback_growth                    \
    native_command_unsigned, 14, 9
#define NATIVE_CMD_LAYOUT_create_hitbox_3_rebound native_command_unsigned, 0, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_3_weight_set_knockback                \
    native_command_unsigned, 5, 9
#define NATIVE_CMD_LAYOUT_create_hitbox_4_base_knockback                      \
    native_command_unsigned, 23, 9
#define NATIVE_CMD_LAYOUT_create_hitbox_4_element                             \
    native_command_unsigned, 18, 5
#define NATIVE_CMD_LAYOUT_create_hitbox_4_hit_aerial                          \
    native_command_unsigned, 0, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_4_hit_grounded                        \
    native_command_unsigned, 1, 1
#define NATIVE_CMD_LAYOUT_create_hitbox_4_hit_sfx_kind                        \
    native_command_unsigned, 2, 5
#define NATIVE_CMD_LAYOUT_create_hitbox_4_hit_sfx_severity                    \
    native_command_unsigned, 7, 3
#define NATIVE_CMD_LAYOUT_create_hitbox_4_shield_damage                       \
    native_command_signed, 10, 8
#define NATIVE_CMD_LAYOUT_create_hitbox_5_x1_b4 native_command_unsigned, 19, 1
#define NATIVE_CMD_LAYOUT_footstep_fx_0_use_alt_bone                          \
    native_command_unsigned, 17, 1
#define NATIVE_CMD_LAYOUT_it_create_hitbox_0_bone                             \
    native_command_unsigned, 13, 7
#define NATIVE_CMD_LAYOUT_it_create_hitbox_0_damage                           \
    native_command_unsigned, 0, 13
#define NATIVE_CMD_LAYOUT_it_create_hitbox_0_hit_group                        \
    native_command_unsigned, 20, 3
#define NATIVE_CMD_LAYOUT_it_create_hitbox_0_id native_command_unsigned, 23, 3
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_base_knockback                   \
    native_command_unsigned, 23, 9
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_element                          \
    native_command_unsigned, 18, 5
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_sfx_kind                         \
    native_command_unsigned, 2, 4
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_sfx_severity                     \
    native_command_unsigned, 6, 3
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_shield_damage                    \
    native_command_signed, 9, 8
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_x40_b0                           \
    native_command_unsigned, 17, 1
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_x40_b2                           \
    native_command_unsigned, 0, 1
#define NATIVE_CMD_LAYOUT_it_create_hitbox_4_x40_b3                           \
    native_command_unsigned, 1, 1
#define NATIVE_CMD_LAYOUT_light_rot1_x native_command_signed, 13, 13
#define NATIVE_CMD_LAYOUT_light_rot1_yz native_command_signed, 0, 13
#define NATIVE_CMD_LAYOUT_light_rot2_light_enable                             \
    native_command_unsigned, 25, 1
#define NATIVE_CMD_LAYOUT_light_rot2_x native_command_signed, 12, 12
#define NATIVE_CMD_LAYOUT_light_rot2_yz native_command_signed, 0, 12
#define NATIVE_CMD_LAYOUT_part_anim_unk1 native_command_signed, 19, 7
#define NATIVE_CMD_LAYOUT_part_anim_unk2 native_command_signed, 12, 7
#define NATIVE_CMD_LAYOUT_part_anim_unk3 native_command_unsigned, 0, 12
#define NATIVE_CMD_LAYOUT_pseudo_random_sfx_0_behavior                        \
    native_command_unsigned, 6, 4
#define NATIVE_CMD_LAYOUT_pseudo_random_sfx_0_panning                         \
    native_command_unsigned, 10, 8
#define NATIVE_CMD_LAYOUT_pseudo_random_sfx_0_random_range                    \
    native_command_unsigned, 0, 6
#define NATIVE_CMD_LAYOUT_pseudo_random_sfx_0_volume                          \
    native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_pseudo_random_sfx_1_sfx_id                          \
    native_command_unsigned, 0, 32
#define NATIVE_CMD_LAYOUT_set_airborne_state_state                            \
    native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_article_vis_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_cmd_var_idx native_command_unsigned, 24, 2
#define NATIVE_CMD_LAYOUT_set_cmd_var_value native_command_unsigned, 0, 24
#define NATIVE_CMD_LAYOUT_set_dobj_flags_idx native_command_signed, 19, 7
#define NATIVE_CMD_LAYOUT_set_dobj_flags_value native_command_signed, 0, 19
#define NATIVE_CMD_LAYOUT_set_fighter_vis_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_hitbox_damage_idx native_command_unsigned, 23, 3
#define NATIVE_CMD_LAYOUT_set_hitbox_damage_value                             \
    native_command_unsigned, 0, 23
#define NATIVE_CMD_LAYOUT_set_hitbox_scale_idx native_command_unsigned, 23, 3
#define NATIVE_CMD_LAYOUT_set_hitbox_scale_value native_command_unsigned, 0, 23
#define NATIVE_CMD_LAYOUT_set_hitbox_x42_b57_idx native_command_unsigned, 2, 24
#define NATIVE_CMD_LAYOUT_set_hitbox_x42_b57_type native_command_unsigned, 1, 1
#define NATIVE_CMD_LAYOUT_set_hitbox_x42_b57_value                            \
    native_command_unsigned, 0, 1
#define NATIVE_CMD_LAYOUT_set_hurt_state_bone_idx                             \
    native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_set_hurt_state_state native_command_unsigned, 0, 18
#define NATIVE_CMD_LAYOUT_set_jab_combo_disabled native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_jab_rapid_state native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_tex_anim_b native_command_unsigned, 25, 1
#define NATIVE_CMD_LAYOUT_set_tex_anim_frame native_command_signed, 0, 11
#define NATIVE_CMD_LAYOUT_set_tex_anim_idx native_command_signed, 18, 7
#define NATIVE_CMD_LAYOUT_set_tex_anim_idx2 native_command_signed, 11, 7
#define NATIVE_CMD_LAYOUT_set_throw_flags_hit_idx                             \
    native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_0_damage                           \
    native_command_unsigned, 0, 23
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_0_idx native_command_unsigned, 23, 3
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_1_hit_x24                          \
    native_command_unsigned, 14, 9
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_1_hit_x28                          \
    native_command_unsigned, 5, 9
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_1_unk0                             \
    native_command_unsigned, 23, 9
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_2_element                          \
    native_command_unsigned, 19, 4
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_2_hit_x2C                          \
    native_command_unsigned, 23, 9
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_2_sfx_kind                         \
    native_command_unsigned, 12, 4
#define NATIVE_CMD_LAYOUT_set_throw_hitbox_2_sfx_severity                     \
    native_command_unsigned, 16, 3
#define NATIVE_CMD_LAYOUT_smash_charge_0_charge_frames                        \
    native_command_unsigned, 16, 10
#define NATIVE_CMD_LAYOUT_smash_charge_0_charge_rate                          \
    native_command_unsigned, 0, 16
#define NATIVE_CMD_LAYOUT_smash_charge_1_color_anim                           \
    native_command_unsigned, 24, 8
#define NATIVE_CMD_LAYOUT_sound_effect_0_behavior                             \
    native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_sound_effect_1_sfx_id native_command_unsigned, 0, 32
#define NATIVE_CMD_LAYOUT_sound_effect_2_panning native_command_unsigned, 0, 8
#define NATIVE_CMD_LAYOUT_sound_effect_2_volume native_command_unsigned, 8, 8
#define NATIVE_CMD_LAYOUT_spawn_gfx_0_boneId native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_spawn_gfx_0_destroyOnStateChange                    \
    native_command_unsigned, 16, 1
#define NATIVE_CMD_LAYOUT_spawn_gfx_0_useCommonBoneIDs                        \
    native_command_unsigned, 17, 1
#define NATIVE_CMD_LAYOUT_spawn_gfx_0_useUnkBone native_command_unsigned, 15, 1
#define NATIVE_CMD_LAYOUT_spawn_gfx_1_gfxID native_command_unsigned, 16, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_1_unkFloat native_command_unsigned, 0, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_2_offsetY native_command_signed, 0, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_2_offsetZ native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_3_offsetX native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_3_rangeZ native_command_unsigned, 0, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_4_rangeX native_command_unsigned, 0, 16
#define NATIVE_CMD_LAYOUT_spawn_gfx_4_rangeY native_command_unsigned, 16, 16
#define NATIVE_CMD_LAYOUT_stage_sfx_0_pitch_select                            \
    native_command_unsigned, 0, 8
#define NATIVE_CMD_LAYOUT_stage_sfx_0_sfx_base native_command_unsigned, 16, 10
#define NATIVE_CMD_LAYOUT_stage_sfx_0_x2_b0_7 native_command_unsigned, 8, 8
#define NATIVE_CMD_LAYOUT_stage_sfx_1_sfx_id native_command_unsigned, 0, 32
#define NATIVE_CMD_LAYOUT_stage_sfx_2_x2_b0_15 native_command_unsigned, 0, 16
#define NATIVE_CMD_LAYOUT_stage_sfx_3_x2_b0_7 native_command_unsigned, 8, 8
#define NATIVE_CMD_LAYOUT_stage_sfx_3_x3_b0_7 native_command_unsigned, 0, 8
#define NATIVE_CMD_LAYOUT_unk_timer native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk_unk native_command_unsigned, 26, 6
#define NATIVE_CMD_LAYOUT_unk0_opcode native_command_unsigned, 26, 6
#define NATIVE_CMD_LAYOUT_unk10_unk1 native_command_unsigned, 25, 1
#define NATIVE_CMD_LAYOUT_unk10_unk2 native_command_unsigned, 13, 12
#define NATIVE_CMD_LAYOUT_unk10_unk3 native_command_unsigned, 0, 13
#define NATIVE_CMD_LAYOUT_unk11_unk1 native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk12_unk1 native_command_unsigned, 24, 2
#define NATIVE_CMD_LAYOUT_unk12_unk2 native_command_unsigned, 14, 10
#define NATIVE_CMD_LAYOUT_unk12_unk3 native_command_unsigned, 0, 14
#define NATIVE_CMD_LAYOUT_unk13_unk1 native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_unk13_unk2 native_command_unsigned, 0, 18
#define NATIVE_CMD_LAYOUT_unk14_unk1 native_command_unsigned, 18, 8
#define NATIVE_CMD_LAYOUT_unk15_unk1 native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk16_unk3 native_command_signed, 25, 1
#define NATIVE_CMD_LAYOUT_unk16_unk4 native_command_signed, 0, 25
#define NATIVE_CMD_LAYOUT_unk17_unk1 native_command_signed, 0, 26
#define NATIVE_CMD_LAYOUT_unk18_damage_amount native_command_signed, 0, 26
#define NATIVE_CMD_LAYOUT_unk19_unk1 native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk20_unk1 native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk21_unk1 native_command_unsigned, 25, 1
#define NATIVE_CMD_LAYOUT_unk21_unk2 native_command_unsigned, 17, 8
#define NATIVE_CMD_LAYOUT_unk27_value native_command_unsigned, 0, 26
#define NATIVE_CMD_LAYOUT_unk33_unk0 native_command_unsigned, 13, 13
#define NATIVE_CMD_LAYOUT_unk33_unk1 native_command_unsigned, 0, 13
#define NATIVE_CMD_LAYOUT_unk9_unk1 native_command_unsigned, 13, 13
#define NATIVE_CMD_LAYOUT_unk9_unk2 native_command_unsigned, 0, 13
#define NATIVE_CMD_LAYOUT_unk_fx_0_x1_b0_7 native_command_unsigned, 16, 8
#define NATIVE_CMD_LAYOUT_wind_fx_0_bone native_command_unsigned, 0, 8
#define NATIVE_CMD_LAYOUT_wind_fx_1_timer native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_wind_fx_1_x native_command_signed, 0, 16
#define NATIVE_CMD_LAYOUT_wind_fx_2_mag native_command_signed, 0, 16
#define NATIVE_CMD_LAYOUT_wind_fx_2_y native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_wind_fx_3_angle native_command_signed, 16, 16
#define NATIVE_CMD_LAYOUT_wind_fx_3_decay native_command_signed, 0, 16

#endif
