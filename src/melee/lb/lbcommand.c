#include "lbcommand.h"

#include "inlines.h"
#include "lb_0219.h"
#include "types.h"

#ifdef MELEE_NATIVE
#include <stddef.h>

_Static_assert(sizeof(union CmdUnion) == 4,
               "Command cursors must advance by one serialized word");
_Static_assert(offsetof(ColorOverlay, x8_ptr1) == offsetof(CommandInfo, u),
               "Color and fighter command cursors must share an offset");
_Static_assert(offsetof(ColorOverlay, xC_loop) ==
                   offsetof(CommandInfo, loop_count),
               "Color and fighter command stack depths must share an offset");
_Static_assert(offsetof(ColorOverlay, x10_ptr2) ==
                       offsetof(CommandInfo, event_return[0]) &&
                   offsetof(ColorOverlay, x14) ==
                       offsetof(CommandInfo, event_return[1]) &&
                   offsetof(ColorOverlay, x18_alloc) ==
                       offsetof(CommandInfo, event_return[2]) &&
                   offsetof(ColorOverlay, x1c) ==
                       offsetof(CommandInfo, event_return[3]) &&
                   offsetof(ColorOverlay, x20) ==
                       offsetof(CommandInfo, event_return[4]),
               "Color commands must retain full-width return addresses");
#endif

void (*lbCommand_803B9840[16])(CommandInfo*) = {
    Command_00, Command_01, Command_02, Command_03, Command_04, Command_05,
    Command_06, Command_07, Command_08, Command_09, NULL,       NULL,
    NULL,       NULL,       NULL,       NULL
};

/// End the command stream. The caller stops when the cursor is NULL.
void Command_00(CommandInfo* info)
{
    info->u = NULL;
}

/// Add a relative wait to the current timer, retaining any frame overshoot.
void Command_01(CommandInfo* info)
{
    info->timer += CMD_FIELD(info->u, Command_00, value);
    NEXT_CMD(info);
}

/// Wait until the animation reaches the requested absolute frame.
void Command_02(CommandInfo* info)
{
    info->timer = CMD_FIELD(info->u, Command_02, value) - info->frame_count;
    NEXT_CMD(info);
}

/// Push the loop body address, then its remaining iteration count.
void Command_03(CommandInfo* info)
{
#ifdef MELEE_NATIVE
    if (info->loop_count > 3) {
        abort();
    }
#endif
    info->event_return[info->loop_count++] = info->u + 1;
    info->event_return[info->loop_count++] =
        (union CmdUnion*) (uintptr_t) CMD_FIELD(info->u, Command_03, value);
    NEXT_CMD(info);
}

/// Repeat the loop body or pop both loop entries when the count reaches zero.
void Command_04(CommandInfo* info)
{
#ifdef MELEE_NATIVE
    u32 remaining;
    if (info->loop_count < 2 || info->loop_count > 5) {
        abort();
    }
    remaining = (u32) (uintptr_t) info->event_return[info->loop_count - 1] - 1;
    info->event_return[info->loop_count - 1] =
        (union CmdUnion*) (uintptr_t) remaining;
#else
    /* Decrement the count at event_return[loop_count - 1] as a word, not a
     * command pointer. Keep this base and index form for the matching build.
     */
    u32* words = (u32*) info;
    words[info->loop_count + 3] -= 1;
#endif

    if (info->event_return[info->loop_count - 1] != NULL) {
        info->u = info->event_return[info->loop_count - 2];
        return;
    }
    NEXT_CMD(info);
    info->loop_count -= 2;
}

/// Read the target from the next word and push the word after it for return.
void Command_05(CommandInfo* info)
{
    NEXT_CMD(info);
#ifdef MELEE_NATIVE
    if (info->loop_count >= 5) {
        abort();
    }
#endif
    info->event_return[info->loop_count++] = info->u + 1;
#ifdef MELEE_NATIVE
    info->u = native_archive_command_target(info->u);
#else
    info->u = info->u->Command_05.ptr;
#endif
}

/// Pop the return address saved by Command_05.
void Command_06(CommandInfo* info)
{
#ifdef MELEE_NATIVE
    if (info->loop_count == 0 || info->loop_count > 5) {
        abort();
    }
#endif
    info->u = info->event_return[info->loop_count -= 1];
}

/// Jump to the address stored in the next word.
void Command_07(CommandInfo* info)
{
    NEXT_CMD(info);
#ifdef MELEE_NATIVE
    info->u = native_archive_command_target(info->u);
#else
    info->u = info->u->Command_07.ptr;
#endif
}

/// Wait until the caller sees an animation frame below one frame-speed step.
void Command_08(CommandInfo* info)
{
    NEXT_CMD(info);
    info->timer = F32_MAX;
}

void Command_09(CommandInfo* info)
{
    lbBgFlash_80021C48(CMD_FIELD(info->u, Command_09, param_1),
                       CMD_FIELD(info->u, Command_09, param_2));
    NEXT_CMD(info);
}

bool Command_Execute(CommandInfo* info, u32 opcode)
{
    if (opcode < 10) {
        lbCommand_803B9840[opcode](info);
        return true;
    }
    return false;
}
