#ifndef MELEE_LB_INLINES_H
#define MELEE_LB_INLINES_H

#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>

#ifdef MELEE_NATIVE
#include <command.h>
#else
#define CMD_FIELD(pointer, kind, field) ((pointer)->kind.field)
#define CMD_U16(pointer, index) (((u16*) (pointer))[index])
#define CMD_S16(pointer, index) (((s16*) (pointer))[index])
#define CMD_U32(pointer) (*(u32*) (pointer))
#endif

/// @todo Is a macro the best way?
#define SKIP_CMD(cmd, n)                                                      \
    do {                                                                      \
        int i;                                                                \
        for (i = 0; i < (n); i++) {                                           \
            ++(cmd)->u;                                                       \
        }                                                                     \
    } while (0);

#define NEXT_CMD(cmd)                                                         \
    do {                                                                      \
        ++(cmd)->u;                                                           \
    } while (0);

static inline void lbCardGame_SetupArchive(void)
{
    lbCardNew_AllocWorkArea();
    lbCardGame_LoadArchive(0);
    lbCardGame_UpdatePowerTime();
}

#endif
