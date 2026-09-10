#include "hsd_3B27.h"

#include <stddef.h>
#include <string.h>

#include "hsd_3A94.h"

typedef struct {
    HsdCardArg type;
    HsdCardArg f1;
    HsdCardArg f2;
    HsdCardArg f3;
    HsdCardArg f4;
    HsdCardArg f5;
} HsdCmdEntry;

#ifdef MELEE_NATIVE
#define CMD_QUEUE(base) ((HsdCmdEntry*) hsd_native_card_queue)
#else
#define CMD_QUEUE(base) ((HsdCmdEntry*) ((base) + 0x1210))
#endif

int hsd_803B27F4(const s32* arg0, const char* arg1, HsdCardArg arg2,
                 HsdCardArg arg3, void (*arg4)(int, int))
{
    s32 read_idx = hsd_804D7990;
    u8* base = hsd_804D1138;
    s32 write_idx = hsd_804D7994;
    HsdCmdEntry* entry;

    if (read_idx == write_idx) {
        if (CMD_QUEUE(base)[read_idx].type != 0) {
            return -265;
        }
    }

    entry = &CMD_QUEUE(base)[write_idx];
    {
        s32 next = write_idx + 1;
        entry->type = 6;
        entry->f1 = (HsdCardArg) arg0;
        entry->f2 = (HsdCardArg) arg1;
        entry->f3 = arg2;
        entry->f4 = arg3;
        entry->f5 = (HsdCardArg) arg4;
        hsd_804D7994 = next % 32;
    }

    return 0;
}

int hsd_803B286C(const s32* arg0, UNK_T arg1, const char* arg2,
                 HsdCardArg arg3, HsdCardArg arg4, void (*arg5)(int, int))
{
    u8* base = hsd_804D1138;

    memcpy(((CardState*) arg0)->x370, arg2, 64);

    {
        s32 write_idx;
        s32 read_idx = hsd_804D7990;

        if (read_idx == (write_idx = hsd_804D7994)) {
            if (CMD_QUEUE(base)[read_idx].type != 0) {
                return -265;
            }
        }

        CMD_QUEUE(base)[write_idx].type = 3;
        CMD_QUEUE(base)[write_idx].f1 = (HsdCardArg) arg0;
        CMD_QUEUE(base)[write_idx].f2 = (HsdCardArg) arg1;
        CMD_QUEUE(base)[write_idx].f3 = arg3;
        CMD_QUEUE(base)[write_idx].f4 = arg4;
        CMD_QUEUE(base)[write_idx].f5 = (HsdCardArg) arg5;
        hsd_804D7994 = (write_idx + 1) % 32;
    }

    return 0;
}

int hsd_803B2928(const s32* arg0, const char* arg1, HsdCardArg arg2,
                 HsdCardArg arg3, void (*arg4)(int, int))
{
    u8* base = hsd_804D1138;

    memcpy(((CardState*) arg0)->x370, arg1, 64);

    {
        s32 write_idx;
        s32 read_idx = hsd_804D7990;

        if (read_idx == (write_idx = hsd_804D7994)) {
            if (CMD_QUEUE(base)[read_idx].type != 0) {
                return -265;
            }
        }

        CMD_QUEUE(base)[write_idx].type = 4;
        CMD_QUEUE(base)[write_idx].f1 = (HsdCardArg) arg0;
        CMD_QUEUE(base)[write_idx].f3 = arg2;
        CMD_QUEUE(base)[write_idx].f4 = arg3;
        CMD_QUEUE(base)[write_idx].f5 = (HsdCardArg) arg4;
        hsd_804D7994 = (write_idx + 1) % 32;
    }

    return 0;
}

int hsd_803B29D8(const s32* ctx, int channel, const u8* data, UNK_T callback)
{
    s32 read_idx = hsd_804D7990;
    u8* base = hsd_804D1138;
    s32 write_idx = hsd_804D7994;
    HsdCmdEntry* entry;

    if (read_idx == write_idx) {
        if (CMD_QUEUE(base)[read_idx].type != 0) {
            return -265;
        }
    }

    entry = &CMD_QUEUE(base)[write_idx];
    {
        s32 next = write_idx + 1;
        entry->type = 1;
        entry->f1 = (HsdCardArg) ctx;
        entry->f2 = channel;
        entry->f3 = (HsdCardArg) data;
        entry->f5 = (HsdCardArg) callback;
        hsd_804D7994 = next % 32;
    }

    return 0;
}

int hsd_803B2A4C(const s32* arg0, int arg1, const u8* arg2,
                 void (*arg3)(int, int))
{
    u8* base = hsd_804D1138;
    s32 read_idx;
    s32 write_idx;
    HsdCmdEntry* entry;

    if (((const CardState*) arg0)->x4C[arg1] <= 0) {
        return -257;
    }

    read_idx = hsd_804D7990;
    write_idx = hsd_804D7994;

    if (read_idx == write_idx) {
        if (CMD_QUEUE(base)[read_idx].type != 0) {
            return -265;
        }
    }

    entry = &CMD_QUEUE(base)[write_idx];
    {
        s32 next = write_idx + 1;
        entry->type = 2;
        entry->f1 = (HsdCardArg) arg0;
        entry->f2 = arg1;
        entry->f3 = (HsdCardArg) arg2;
        entry->f5 = (HsdCardArg) arg3;
        hsd_804D7994 = next % 32;
    }

    return 0;
}

int hsd_803B2ADC(s32* ctx, UNK_T data)
{
    CardState* state = (CardState*) ctx;

    memcpy(&state->x3B0, data, 18);
    state->x24 = hsd_803AC340(&state->x3B0);
    return 0;
}
