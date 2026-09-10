#ifndef MELEE_FT_CHARA_FTKOOPA_TYPES_H
#define MELEE_FT_CHARA_FTKOOPA_TYPES_H

#include <Runtime/platform.h>

#include <placeholder.h>

struct ftKoopa_FighterVars {
    /* 0x222C */ float x222C;
    /* 0x2230 */ float x2230;
};

union ftKoopa_MotionVars {
    /// @todo Proper state name.
    struct ftKoopa_State1Vars {
        /* Native builds use integers so each aliased value remains in its
         * original four-byte slot.  x4 can contain an item ID, so it cannot be
         * represented as a native bool. */
#ifdef MELEE_NATIVE
        s32 x0;
        s32 x4;
        s32 x8;
        s32 xC;
#else
        UNK_T x0;
        bool x4;
        UNK_T x8;
        bool xC;
#endif
    } unk1;
    /// @todo Possibly #ftKoopa_State1Vars.
    struct ftKoopa_SpecialSVars {
#ifdef MELEE_NATIVE
        s32 b_held;
        s32 x4;
#else
        /* fp+2340 */ bool b_held;
        /* fp+2344 */ bool x4;
#endif
        /* fp+2348 */ int facing_dir;
#ifdef MELEE_NATIVE
        /* fp+234C */ s32 xC;
#else
        /* fp+234C */ bool xC;
#endif
        /* fp+234C */ s32 x10;
        /* fp+2350 */ s32 x14;
        /* fp+2354 */ s32 x18;
    } specials;
};

#ifdef MELEE_NATIVE
_Static_assert(offsetof(struct ftKoopa_State1Vars, x4) == 0x4,
               "Koopa motion slots must stay four-byte aligned");
_Static_assert(offsetof(struct ftKoopa_SpecialSVars, x4) == 0x4,
               "Koopa special motion slots must stay four-byte aligned");
_Static_assert(sizeof(struct ftKoopa_State1Vars) == 0x10,
               "Koopa motion state size changed");
_Static_assert(sizeof(struct ftKoopa_SpecialSVars) == 0x1C,
               "Koopa special motion state size changed");
#endif

typedef struct _ftKoopaAttributes {
    float x0;
    s32 x4;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    float x1C;
    s32 x20;
    float x24;
    float x28;
    u32 x2C;
    float x30;
    float x34;
    float x38;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C;
    u32 unk50;
    float x54;
    float x58;
    float x5C;
    float x60;
    float x64;
    float x68;
    float x6C;
    float x70;
    float x74;
    float x78;
    float x7C;
    float x80;
    float x84;
    float x88;
    float x8C;
    float x90;
    float x94;
    float x98;
    float x9C;
} ftKoopaAttributes;

typedef struct _ftKoopaVars {
    float x0;
    float x4;
} ftKoopaVars;

#endif
