#include "gobjplink.h"

#include "debug.h"
#include "gobj.h"
#include "gobjgxlink.h"
#include "gobjobject.h"
#include "gobjproc.h"
#include "gobjuserdata.h"
#include "objalloc.h"

void GObj_PReorder(HSD_GObj* gobj, HSD_GObj* predecessor)
{
    u8 link = gobj->p_link;
    gobj->prev = predecessor;
    if (predecessor != NULL) {
        gobj->next = predecessor->next;
        predecessor->next = gobj;
    } else {
        gobj->next = HSD_GObjPLinkHead(link);
        *HSD_GObjPLinkSlot(link) = gobj;
    }
    if (gobj->next != NULL) {
        gobj->next->prev = gobj;
    } else {
        plinklow_gobjs[link] = gobj;
    }
}

extern HSD_ObjAllocData gobj_alloc_data;

// Keep this wrapper: a direct call changes CreateGObj register allocation.
static inline HSD_GObj* allocateObject(void)
{
    return HSD_ObjAlloc(&gobj_alloc_data);
}

static inline void insertAfterEqualPriority(HSD_GObj* gobj)
{
    HSD_GObj* candidate = plinklow_gobjs[gobj->p_link];
    while (candidate != NULL && candidate->p_priority > gobj->p_priority) {
        candidate = candidate->prev;
    }
    GObj_PReorder(gobj, candidate);
}

static inline void insertBeforeEqualPriority(HSD_GObj* gobj)
{
    HSD_GObj* candidate = HSD_GObjPLinkHead(gobj->p_link);
    while (candidate != NULL && candidate->p_priority < gobj->p_priority) {
        candidate = candidate->next;
    }
    GObj_PReorder(gobj, candidate != NULL ? candidate->prev
                                          : plinklow_gobjs[gobj->p_link]);
}

HSD_GObj* CreateGObj(s32 where, u16 classifier, u8 p_link, u8 priority,
                     HSD_GObj* position)
{
    HSD_GObj* gobj;

    HSD_ASSERT(0xA8, p_link <= HSD_GObjLibInitData.p_link_max);
    if ((gobj = allocateObject()) == NULL) {
        return NULL;
    }
    gobj->classifier = classifier;
    gobj->p_link = p_link;
    gobj->gx_link = HSD_GOBJ_GXLINK_NONE;
    gobj->p_priority = priority;
    gobj->render_priority = 0;
    gobj->obj_kind = HSD_GOBJ_OBJ_NONE;
    gobj->user_data_kind = HSD_GOBJ_USER_DATA_NONE;
    gobj->prev_gx = NULL;
    gobj->next_gx = NULL;
    gobj->proc = NULL;
    gobj->render_cb = NULL;
    gobj->gxlink_prios = 0;
    gobj->hsd_obj = NULL;
    gobj->user_data = NULL;
    gobj->user_data_remove_func = NULL;
    switch (where) {
    case 0:
        insertAfterEqualPriority(gobj);
        break;
    case 1:
        insertBeforeEqualPriority(gobj);
        break;
    case 2: // insert after position
        GObj_PReorder(gobj, position);
        break;
    case 3: // insert before position
        GObj_PReorder(gobj, position->prev);
        break;
    }
    return gobj;
}

HSD_GObj* GObj_Create(u16 classifier, u8 p_link, u8 priority)
{
    return CreateGObj(0, classifier, p_link, priority, NULL);
}

static inline void unlinkObject(HSD_GObj* gobj)
{
    if (gobj->prev != NULL) {
        gobj->prev->next = gobj->next;
    } else {
        *HSD_GObjPLinkSlot(gobj->p_link) = gobj->next;
    }
    if (gobj->next != NULL) {
        gobj->next->prev = gobj->prev;
    } else {
        plinklow_gobjs[gobj->p_link] = gobj->prev;
    }
}

void HSD_GObjFree(HSD_GObj* gobj)
{
    HSD_ASSERT(0x171, gobj);
    // The scheduler applies changes to its active owner after the callback.
    if (!HSD_GObj_DelayedProcInfo.in_delayed_proc &&
        gobj == HSD_GObj_CurrentInvokedProcGObj)
    {
        HSD_GObj_DelayedProcInfo.delay_remove_gobj = 1;
        return;
    }
    GObj_RemoveUserData(gobj);
    HSD_GObjObject_80390B0C(gobj);
    HSD_GObjProc_RemoveAllProcs(gobj);
    if (gobj->gx_link != HSD_GOBJ_GXLINK_NONE) {
        HSD_GObjGXLink_8039084C(gobj);
    }
    unlinkObject(gobj);
    HSD_ObjFree(&gobj_alloc_data, gobj);
}

void HSD_GObjPLink_ChangeGObjPri_Unk(u32 where, HSD_GObj* gobj, u8 p_link,
                                     u8 priority, HSD_GObj* position)
{
    HSD_GObjProc* detached_processes;
    HSD_GObjProc* next_owned_process;
    HSD_GObjProc* process;
    s32 next_tag;
    s32 previous_tag;

    u8 _[8];

    HSD_ASSERT(0x1A3, p_link <= HSD_GObjLibInitData.p_link_max);
    // The scheduler applies changes to its active owner after the callback.
    if (!HSD_GObj_DelayedProcInfo.in_delayed_proc &&
        gobj == HSD_GObj_CurrentInvokedProcGObj)
    {
        HSD_GObj_DelayedProcInfo.delay_change_gobj_pri = 1;
        HSD_GObj_DelayedProcInfo.type = where;
        HSD_GObj_DelayedProcInfo.p_link = p_link;
        HSD_GObj_DelayedProcInfo.p_prio = priority;
        HSD_GObj_DelayedProcInfo.gobj = position;
        return;
    }
    // Reverse the owner list so reinsertion restores its original order.
    process = gobj->proc;
    detached_processes = NULL;
    while (process != NULL) {
        HSD_GObjProc_UnqueueProc(process);
        next_owned_process = process->child;
        process->child = detached_processes;
        detached_processes = process;
        process = next_owned_process;
    }
    gobj->proc = NULL;
    unlinkObject(gobj);
    gobj->p_link = p_link;
    gobj->p_priority = priority;
    switch (where) {
    case 0:
        insertAfterEqualPriority(gobj);
        break;
    case 1:
        insertBeforeEqualPriority(gobj);
        break;
    case 2: // insert after position
        GObj_PReorder(gobj, position);
        break;
    case 3: // insert before position
        GObj_PReorder(gobj, position->prev);
        break;
    }
    // Refresh a tag that would match the next traversal. Keep the current tag.
    previous_tag = HSD_GObj_804D783C == 0 ? 2 : HSD_GObj_804D783C - 1;
    next_tag = previous_tag == 0 ? 2 : previous_tag - 1;
    process = detached_processes;
    while (process != NULL) {
        next_owned_process = process->child;
        HSD_GObjProc_QueueProc(process);
        if (process->flags_3 == next_tag) {
            process->flags_3 = previous_tag;
        }
        process = next_owned_process;
    }
}
