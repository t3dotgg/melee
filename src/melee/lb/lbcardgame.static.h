#ifndef __GALE01_019880
#define __GALE01_019880

#include <Runtime/platform.h>

#include <melee/lb/lbcardgame.h> // IWYU pragma: export
#include <melee/sc/types.h>
#include <sysdolphin/baselib/hsd_3A94.h>

struct lb_80433318_t {
    /* +0  */ int x0;
    /* +4  */ int x4;
    /* +8  */ int x8;
    /* +C  */ bool xC;
    /* +10 */ int x10;
    /* +14 */ int x14;
    /* +18 */ bool enable;
    /* +1C */ char _1C[0x40];
    /* +5C */ HsdCardArg* x5C;
    /* +60 */ int x60;
    /* +64 */ SceneDesc* x64;
};
ASSERT_SIZE(struct lb_80433318_t, 0x68);

/* 433318 */ static struct lb_80433318_t lb_80433318;

#endif
