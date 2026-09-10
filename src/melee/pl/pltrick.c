#include "pltrick.h"

#include "pl_040D.h"
#include "player.h"
#include "plbonus.h"
#include "plbonuslib.h"
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/inlines.h>
#include <melee/if/ifmagnify.h>
#include <sysdolphin/baselib/debug.h>

/* 037F00 */ static void fn_80037F00(Fighter*, Fighter*, ft_800898B4_t*, s32,
                                     s32);

int pl_803BCE70[16] = {
    -1, 0x2F, 0x30, 0x31, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

int pl_80037B2C(struct plActionStats* action_stats, int h_player,
                int attack_id)
{
    HSD_ASSERT(0x89, 0 <= h_player && h_player < 8);
    if (attack_id < StatsAttack_Count) {
        return action_stats->x504[attack_id] & (1 << (u8) h_player);
    }
}

void pl_80037BC0(struct plAttackStats* stats, union Struct2070* attack_event)
{
    stats->total++;
    stats->by_attack_counts[attack_event->x2073]++;
    if (attack_event->count_thrown_items) {
        stats->thrown_item_count++;
    }
    if (attack_event->count_aerials) {
        stats->aerials_count++;
    }
    if (attack_event->count_specials) {
        stats->specials_count++;
    }
    if (attack_event->count_x1A0) {
        stats->x1A0_count++;
    }
    if (attack_event->count_x1A4) {
        stats->x1A4_count++;
    }
}

// Keep the extra inline level so matching builds retain the counter calls.
static inline void countHitStats(struct plAttackStats* stats,
                                 union Struct2070* attack_event)
{
    pl_80037BC0(stats, attack_event);
}

void pl_80037C60(Fighter_GObj* fighter_gobj, s32 previous_event_bits)
{
    Fighter* fighter;
    plActionStats* action_stats;
    u8 attack_id;
    union Struct2070 previous_event;
    union Struct2070 attack_event;

    fighter = GET_FIGHTER(fighter_gobj);
    action_stats = Player_GetActionStats(fighter->player_id);
    previous_event = *(union Struct2070*) &previous_event_bits;

    if ((int) fighter->x2070.x2072_b2) {
        action_stats->x5BC_b0 = 1;
    }
    if (fighter->x2070.x2072_b1) {
        action_stats->x5BC_b1 = 1;
    }
    attack_id = 1;
    if (fighter->x2070.x2072_b0) {
        action_stats->x5BC_b2 = attack_id;
    }
    if (fighter->x2070.x2071_b7) {
        action_stats->x5BC_b3 = attack_id;
    }
    attack_id = fighter->x2070.x2073;
    if ((attack_id != StatsAttack_None) && (attack_id != previous_event.x2073))
    {
        if (attack_id >= StatsAttack_Count) {
            if (!fighter->x221F_b4) {
                action_stats->by_attack_hi[attack_id]++;
            }
        } else {
            attack_event.x2070_int = fighter->x2070.x2070_int;
            action_stats->attacks.total++;
            action_stats->attacks.by_attack_counts[attack_event.x2073]++;
            if (attack_event.count_thrown_items) {
                action_stats->attacks.thrown_item_count++;
            }
            if (attack_event.count_aerials) {
                action_stats->attacks.aerials_count++;
            }
            if (attack_event.count_specials) {
                action_stats->attacks.specials_count++;
            }
            if (attack_event.count_x1A0) {
                action_stats->attacks.x1A0_count++;
            }
            if (attack_event.count_x1A4) {
                action_stats->attacks.x1A4_count++;
            }
        }
    }
}

void pl_80037DF4(HSD_GObj* fighter_gobj, union Struct2070* attack_event)
{
    Fighter* fighter = GET_FIGHTER(fighter_gobj);
    union Struct2070 event_copy;
    plActionStats* action_stats = Player_GetActionStats(fighter->player_id);
    event_copy = *attack_event;
    action_stats->attacks.total++;
    action_stats->attacks.by_attack_counts[event_copy.x2073]++;
    if (event_copy.count_thrown_items) {
        action_stats->attacks.thrown_item_count++;
    }
    if (event_copy.count_aerials) {
        action_stats->attacks.aerials_count++;
    }
    if (event_copy.count_specials) {
        action_stats->attacks.specials_count++;
    }
    if (event_copy.count_x1A0) {
        action_stats->attacks.x1A0_count++;
    }
    if (event_copy.count_x1A4) {
        action_stats->attacks.x1A4_count++;
    }
}

void pl_80037ECC(HSD_GObj* fighter_gobj)
{
    Fighter* fighter = GET_FIGHTER(fighter_gobj);
    plActionStats* action_stats = Player_GetActionStats(fighter->player_id);
    action_stats->attacks.x1A8++;
}

static void fn_80037F00(Fighter* attacker, Fighter* victim,
                        ft_800898B4_t* hit_data, s32 attacked_from_behind,
                        s32 previous_source_player)
{
    f32 previous_knockback;
    pl_804D6470_t* bonus_params;
    PAD_STACK(8);

    if (victim->x221C_b6) {
        previous_knockback = victim->dmg.x18d8.kb_applied1;
    } else {
        previous_knockback = 0.0f;
    }

    if (hit_data != NULL) {
        victim->dmg.x18d8 = *hit_data;
    } else {
        victim->dmg.x18d8.x0 = 0;
        victim->dmg.x18d8.x4 = 0;
        victim->dmg.x18d8.kb_applied1 = 0.0f;
        victim->dmg.x18d8.xC = 6;
        victim->dmg.x18d8.x10_b0 = 0;
        victim->dmg.x18d8.x11_b3 = 0;
        victim->dmg.x18d8.x10_b1 = 0;
        victim->dmg.x18d8.x10_b2 = 0;
        victim->dmg.x18d8.x10_b3 = 0;
        victim->dmg.x18d8.x10_b4 = 0;
        victim->dmg.x18d8.x10_b5 = 0;
        victim->dmg.x18d8.x10_b6 = 0;
        victim->dmg.x18d8.x10_b7 = 0;
        victim->dmg.x18d8.x11_b0 = 0;
        victim->dmg.x18d8.x11_b1 = 0;
        victim->dmg.x18d8.x11_b2 = 0;
        victim->dmg.x18d8.x11_b4 = 0;
    }

    victim->dmg.x18d8.x4 = (int) victim->dmg.x1830_percent;

    victim->dmg.x18d8.x11_b0 = attacked_from_behind;

    if (ifMagnify_802FB6E8(victim->player_id) != 0) {
        victim->dmg.x18d8.x11_b1 = 1;
    }

    if (victim->motion_id == 0xFD) {
        victim->dmg.x18d8.x11_b2 = 1;
    }

    if (victim->dmg.x18c4_source_ply != previous_source_player &&
        victim->dmg.x18d8.x10_b0)
    {
        bonus_params = pl_80038914();
        if (previous_knockback >= bonus_params->x14) {
            victim->dmg.x18d8.x11_b3 = 1;
        }
    }

    if (attacker != NULL) {
        if (ft_80089914(attacker->gobj, victim->dmg.x18d4.x3) != 0 &&
            (victim->victim_gobj == NULL ||
             attacker->gobj != victim->victim_gobj))
        {
            victim->dmg.x18d8.x11_b4 = 1;
        }
        hit_data->x10_b7 = 1;
    }
}

void pl_80038144(HSD_GObj* attacker_gobj, HSD_GObj* victim_gobj,
                 s32 attack_event_bits, ft_800898B4_t* hit_data,
                 u16 attack_instance, s32 grounded, s32 previous_source_player)
{
    Fighter* attacker_reload;
    Fighter* attacker;
    Fighter* victim;
    plActionStats* attacker_stats;
    plActionStats* hit_stats;
    s32 attacked_from_behind;
    u8 attack_id;
    u8 counted_attack_id;
    s32 h_player;
    s32 x18d4_x3;
    s32 count_rear_hit;
    union Struct2070 attack_event;
    union Struct2070 recorded_event;
    union Struct2070 counted_event;
    union Struct2070 hit_event;
    PAD_STACK(16);

    if (attacker_gobj != NULL) {
        attacker = GET_FIGHTER(attacker_gobj);
    } else {
        attacker = NULL;
    }

    victim = GET_FIGHTER(victim_gobj);
    attacked_from_behind = 0;
    attack_event = *(union Struct2070*) &attack_event_bits;

    if (attacker != NULL && attack_event.x2073 != StatsAttack_None) {
        f32 facing_dir = victim->facing_dir;

        if (facing_dir * attacker->cur_pos.x < facing_dir * victim->cur_pos.x)
        {
            if ((int) attack_event.x2073 == StatsAttack_Pokeball ||
                (int) attack_event.x2073 == StatsAttack_MSBomb ||
                ((int) attack_event.x2073 >= StatsAttack_Catch &&
                 (int) attack_event.x2073 <= StatsAttack_61))
            {
                count_rear_hit = 0;
            } else {
                count_rear_hit = 1;
            }
            if (count_rear_hit != 0) {
                attacked_from_behind = 1;
            }
        }

        pl_800410F4(attacker->player_id, attacker->x221F_b4, victim->player_id,
                    victim->x221F_b4, attack_event.x2073);
    }

    if (attack_instance == 0 ||
        victim->dmg.x18ec_instancehitby != attack_instance)
    {
        *(s32*) &victim->dmg.x18d4 = attack_event.x2070_int;
        victim->dmg.x18ec_instancehitby = attack_instance;

        if (attacker != NULL && victim->dmg.x18d4.x3 != StatsAttack_None) {
            if (gm_8016B168() && gm_8016B0D4() &&
                attacker->team == victim->team)
            {
                pl_80040D8C(attacker->player_id, attacker->x221F_b4);
            }

            attack_id = attack_event.x2073;
            counted_attack_id = attack_id;
            if (counted_attack_id < StatsAttack_Count) {
                struct plAttackStats* category_stats;

                attacker_stats = Player_GetActionStats(attacker->player_id);
                recorded_event.x2070_int = *(s32*) &victim->dmg.x18d4;
                attacker_reload = GET_FIGHTER(attacker_gobj);
                hit_stats = Player_GetActionStats(
                    GET_FIGHTER(attacker_gobj)->player_id);
                hit_event.x2070_int = recorded_event.x2070_int;
                {
                    union Struct2070* hit_event_ptr = &hit_event;
                    countHitStats(&hit_stats->hits, hit_event_ptr);
                }

                if (hit_data != NULL) {
                    category_stats = &hit_stats->hits;
                    if (hit_data->x10_b0) {
                        category_stats->x1A8++;
                    }
                }

                counted_attack_id = recorded_event.x2073;
                if (counted_attack_id == StatsAttack_99) {
                    pl_8003FE40(attacker_reload->player_id,
                                attacker_reload->x221F_b4);
                }

                if (!hit_data->x10_b7 &&
                    hit_stats->attacks.by_attack_counts[counted_attack_id] >
                        hit_stats->x358_hits
                            .by_attack_counts[counted_attack_id])
                {
                    counted_event.x2070_int = recorded_event.x2070_int;
                    {
                        union Struct2070* counted_event_ptr = &counted_event;
                        countHitStats(&hit_stats->x358_hits,
                                      counted_event_ptr);
                    }

                    if (hit_data != NULL) {
                        category_stats = &hit_stats->x358_hits;
                        if (hit_data->x10_b0) {
                            category_stats->x1A8++;
                        }
                    }

                    pl_8003DFF4(attacker_reload->player_id,
                                attacker_reload->x221F_b4, counted_attack_id);
                }

                if (attacked_from_behind) {
                    attacker_stats->x56C++;
                } else {
                    attacker_stats->x568++;
                }

                if (attack_id == StatsAttack_AttackLw3 ||
                    attack_id == StatsAttack_AttackLw4)
                {
                    attacker_stats->x574++;
                } else {
                    attacker_stats->x570++;
                }

                h_player = victim->player_id;
                {
                    s32 tmp_x18d4_x3 = victim->dmg.x18d4.x3;
                    x18d4_x3 = tmp_x18d4_x3;
                }
                HSD_ASSERT(0x7E, 0 <= h_player && h_player < 8);
                if (x18d4_x3 < StatsAttack_Count) {
                    attacker_stats->x504[x18d4_x3] |= 1 << (u8) h_player;
                }

                pl_8003FE64(attacker->player_id, attacker->x221F_b4);
                pl_8003ED0C(attacker->player_id, attacker->x221F_b4,
                            victim->player_id, victim->x221F_b4,
                            victim->dmg.x1830_percent);
                x18d4_x3 = hit_data->xC;
                pl_8003EA40(attacker->player_id, attacker->x221F_b4,
                            victim->player_id, victim->x221F_b4, x18d4_x3);
                pl_800403FC(attacker->player_id, attacker->x221F_b4,
                            victim->player_id, victim->x221F_b4,
                            victim->dmg.x18d4.x3);
                pl_80040FBC(attacker->player_id, attacker->x221F_b4,
                            victim->player_id, victim->x221F_b4,
                            victim->dmg.x18d4.x3);
            }
        }
    }

    fn_80037F00(attacker, victim, hit_data, attacked_from_behind,
                previous_source_player);
}

void pl_800384DC(HSD_GObj* fighter_gobj, int attack_event_bits,
                 void* hit_data_raw)
{
    Fighter* fighter;
    plActionStats* action_stats;
    u8 attack_id;
    ft_800898B4_t* hit_data;
    union Struct2070 attack_event;
    union Struct2070 event_copy;
    union Struct2070 counted_event;
    union Struct2070 hit_event;
    PAD_STACK(20);

    fighter = GET_FIGHTER(fighter_gobj);
    hit_data = hit_data_raw;
    attack_event = *(union Struct2070*) &attack_event_bits;

    if (attack_event.x2073 != StatsAttack_None &&
        attack_event.x2073 < StatsAttack_Count)
    {
        struct plAttackStats* category_stats;

        event_copy.x2070_int = attack_event.x2070_int;
        action_stats = Player_GetActionStats(fighter->player_id);
        hit_event.x2070_int = attack_event.x2070_int;
        {
            union Struct2070* hit_event_ptr = &hit_event;
            countHitStats(&action_stats->hits, hit_event_ptr);
        }

        if (hit_data != NULL) {
            category_stats = &action_stats->hits;
            if (hit_data->x10_b0) {
                category_stats->x1A8++;
            }
        }

        attack_id = event_copy.x2073;
        if (attack_id == StatsAttack_99) {
            pl_8003FE40(fighter->player_id, fighter->x221F_b4);
        }

        if (!hit_data->x10_b7 &&
            action_stats->attacks.by_attack_counts[attack_id] >
                action_stats->x358_hits.by_attack_counts[attack_id])
        {
            counted_event.x2070_int = attack_event.x2070_int;
            {
                union Struct2070* counted_event_ptr = &counted_event;
                countHitStats(&action_stats->x358_hits, counted_event_ptr);
            }

            if (hit_data != NULL) {
                category_stats = &action_stats->x358_hits;
                if (hit_data->x10_b0) {
                    category_stats->x1A8++;
                }
            }

            pl_8003DFF4(fighter->player_id, fighter->x221F_b4, attack_id);
        }
    }

    fighter->x2074.x2084_b7 = 1;
}

bool pl_80038628(HSD_GObj* fighter_gobj, int kind)
{
    Fighter* fighter;

    HSD_ASSERT(0x1A1, PlATK_AttackNormal_Start <= kind && kind <= PlATK_AttackNormal_End);
#ifdef MELEE_NATIVE
    if (fighter_gobj == NULL) {
        return true;
    }
#endif
    fighter = GET_FIGHTER(fighter_gobj);
    if (pl_803BCE70[kind - 1] == -1) {
        return true;
    }
    if (ftData_80085FD4(fighter, pl_803BCE70[kind - 1])->x8 != 0) {
        return true;
    }
    return false;
}
