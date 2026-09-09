#include "gobj.h"
#include "gobjproc.h"
#include "memory.h"
#include "objalloc.h"

static HSD_GObjLibInitDataType HSD_GObj_80408620 = {
    0x3F,
    0x3F,
    2,
};

void HSD_GObj_803912E0(HSD_GObjLibInitDataType* arg0)
{
    *arg0 = HSD_GObj_80408620;
}

extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_ObjAllocData gobjproc_alloc_data;

void HSD_GObj_80391304(HSD_GObjLibInitDataType* arg0)
{
    GObjFuncs* cur;
    int i;
    int var_r8;
    int nfuncs;
    struct GObjFuncs* var_r4_2;

    HSD_GObj_80391260(arg0);

    HSD_GObjLibInitData = *arg0;

#ifdef MELEE_NATIVE
    HSD_GObj_Entities = HSD_MemAlloc(sizeof(*HSD_GObj_Entities));
#else
    HSD_GObj_Entities =
        HSD_MemAlloc(sizeof(HSD_GObj*) * (arg0->p_link_max + 1));
#endif
    plinklow_gobjs = HSD_MemAlloc(sizeof(HSD_GObj*) * (arg0->p_link_max + 1));
#ifdef MELEE_NATIVE
    for (i = 0; i < 64; i++) {
        HSD_GObjPLinkSlot((u8) i)[0] = NULL;
        if (i <= arg0->p_link_max) {
            plinklow_gobjs[i] = NULL;
        }
    }
#else
    for (i = 0; i < arg0->p_link_max + 1; i++) {
        HSD_GObjPLinkSlot((u8) i)[0] = plinklow_gobjs[i] = NULL;
    }
#endif

    HSD_GObjGXLinkHead =
        HSD_MemAlloc(sizeof(HSD_GObj*) * (arg0->gx_link_max + 2));
    HSD_GObj_804D7820 =
        HSD_MemAlloc(sizeof(HSD_GObj*) * (arg0->gx_link_max + 2));

    for (i = 0; i < arg0->gx_link_max + 2; i++) {
        HSD_GObjGXLinkHead[i] = HSD_GObj_804D7820[i] = 0;
    }

    HSD_GObj_GObjProcHead =
        HSD_MemAlloc(sizeof(HSD_GObjProc*) * (arg0->gproc_pri_max + 1));

    for (i = 0; i < arg0->gproc_pri_max + 1; i++) {
        HSD_GObj_GObjProcHead[i] = 0;
    }

    HSD_GObj_ProcList =
        HSD_MemAlloc(sizeof(HSD_GObjProc*) * (arg0->gproc_pri_max + 1) *
                     (arg0->p_link_max + 1));

    for (i = 0; i < (arg0->gproc_pri_max + 1) * (arg0->p_link_max + 1); i++) {
        HSD_GObj_ProcList[i] = 0;
    }

    HSD_ObjAllocInit(&gobj_alloc_data, sizeof(HSD_GObj), 4);
    HSD_ObjAllocInit(&gobjproc_alloc_data, sizeof(HSD_GObjProc), 4);

    var_r4_2 = arg0->funcs;
    nfuncs = 0;
    while (var_r4_2 != NULL) {
        nfuncs += var_r4_2->size;
        var_r4_2 = var_r4_2->next;
    }

    if (nfuncs != 0) {
        var_r8 = (sizeof(GObjFunc)) * nfuncs;
        HSD_GObj_804D7810 = HSD_MemAlloc(var_r8);

        var_r8 = 0;
        for (cur = arg0->funcs; cur != NULL; cur = cur->next) {
            for (i = 0; i < cur->size; i++, var_r8++) {
                HSD_GObj_804D7810[var_r8] = cur->funcs[i];
            }
        }
    } else {
        HSD_GObj_804D7810 = NULL;
    }

    HSD_GObj_804D783C = 0;
    HSD_GObj_CurrentInvokedProcGObj = NULL;
    HSD_GObj_CurrentInvokedProc = NULL;
    HSD_GObj_DelayedProcInfo.flags = 0;
    HSD_GObj_804D7818 = NULL;
    HSD_GObj_804D7814 = NULL;
}
