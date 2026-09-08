#include "gobjproc.h"

#include "debug.h"
#include "gobj.h"
#include "objalloc.h"

extern HSD_ObjAllocData gobjproc_alloc_data;

// Last scheduled process for one object list and process priority.
static inline HSD_GObjProc** processTailSlot(int p_link, int s_link)
{
    return &HSD_GObj_ProcList[p_link +
                             s_link * (HSD_GObjLibInitData.p_link_max + 1)];
}

void HSD_GObjProc_QueueProc(HSD_GObjProc* gproc)
{
    HSD_GObj* owner;
    HSD_GObjProc* predecessor;
    u8 s_link;
    int p_link;

    owner = gproc->gobj;
    s_link = gproc->s_link;
    p_link = owner->p_link;

    // Find the last process at this priority on the owner or a preceding
    // object. The owner's child list starts with its newest process.
    if (*processTailSlot(p_link, s_link) != NULL) {
        HSD_GObj* candidate_owner = owner;
        while (candidate_owner != NULL) {
            predecessor = candidate_owner->proc;
            while (predecessor != NULL) {
                if (predecessor->s_link == s_link) {
                    if (*processTailSlot(p_link, s_link) == predecessor) {
                        *processTailSlot(p_link, s_link) = gproc;
                    }
                    goto insert_after_predecessor;
                }
                predecessor = predecessor->child;
            }
            candidate_owner = candidate_owner->prev;
        }
    } else {
        *processTailSlot(p_link, s_link) = gproc;
    }

    // Otherwise use the tail of the nearest preceding object list.
    while (p_link-- != 0) {
        predecessor = *processTailSlot(p_link, s_link);
        if (predecessor != NULL) {
            goto insert_after_predecessor;
        }
    }

    // No predecessor exists at this priority. Insert at the scheduler head.
    gproc->next = HSD_GObj_GObjProcHead[s_link];
    HSD_GObj_GObjProcHead[s_link] = gproc;
    gproc->prev = NULL;
    goto link_owner;

insert_after_predecessor:
    gproc->next = predecessor->next;
    predecessor->next = gproc;
    gproc->prev = predecessor;

link_owner:
    if (gproc->next != NULL) {
        gproc->next->prev = gproc;
    }
    gproc->child = owner->proc;
    owner->proc = gproc;
    // Include an insertion immediately after the active process in traversal.
    if (HSD_GObj_DelayedProcInfo.in_delayed_proc &&
        gproc->prev == HSD_GObj_CurrentInvokedProc &&
        gproc->next == HSD_GObj_NextInvokedProc &&
        s_link == HSD_GObj_CurrentInvokedSLink)
    {
        HSD_GObj_NextInvokedProc = gproc;
    }
}

void HSD_GObjProc_UnqueueProc(HSD_GObjProc* gproc)
{
    int p_link = gproc->gobj->p_link;
    int s_link = gproc->s_link;
    if (HSD_GObj_DelayedProcInfo.in_delayed_proc &&
        gproc == HSD_GObj_NextInvokedProc)
    {
        HSD_GObj_NextInvokedProc = gproc->next;
    }
    if (gproc == *processTailSlot(p_link, s_link)) {
        if (gproc->prev != NULL && gproc->prev->gobj->p_link == p_link) {
            *processTailSlot(p_link, s_link) = gproc->prev;
        } else {
            *processTailSlot(p_link, s_link) = NULL;
        }
    }
    if (gproc->prev != NULL) {
        gproc->prev->next = gproc->next;
    } else {
        HSD_GObj_GObjProcHead[s_link] = gproc->next;
    }
    if (gproc->next != NULL) {
        gproc->next->prev = gproc->prev;
    }
}

void HSD_GObjProc_UnlinkProcFromGObj(HSD_GObjProc* gproc)
{
    HSD_GObj* gobj = gproc->gobj;
    HSD_GObjProc_UnqueueProc(gproc);
    if (gobj->proc == gproc) {
        gobj->proc = gproc->child;
    } else {
        HSD_GObjProc* previous_proc = gobj->proc;
        while (previous_proc->child != gproc) {
            previous_proc = previous_proc->child;
        }
        previous_proc->child = gproc->child;
    }
}

static inline void assertProc(HSD_GObjProc* gproc)
{
    HSD_ASSERT(31, gproc);
}

HSD_GObjProc* HSD_GObj_SetupProc(HSD_GObj* gobj, HSD_GObjEvent callback,
                                 u8 pri)
{
    HSD_GObjProc* gproc;

    u8 _[8];

    gproc = HSD_ObjAlloc(&gobjproc_alloc_data);
    assertProc(gproc);
    HSD_ASSERT(216, pri <= HSD_GObjLibInitData.gproc_pri_max);
    gproc->s_link = pri;
    gproc->flags_1 = gproc->flags_2 = 0;
    gproc->flags_3 = 3;
    gproc->gobj = gobj;
    gproc->on_invoke = callback;
    HSD_GObjProc_QueueProc(gproc);
    return gproc;
}

void HSD_GObjProc_RemoveProc(HSD_GObjProc* gproc)
{
    // The scheduler frees its active process after the callback returns.
    if (!HSD_GObj_DelayedProcInfo.in_delayed_proc &&
        gproc == HSD_GObj_CurrentInvokedProc)
    {
        HSD_GObj_DelayedProcInfo.delay_remove_proc = true;
    } else {
        HSD_GObjProc_UnlinkProcFromGObj(gproc);
        HSD_ObjFree(&gobjproc_alloc_data, gproc);
    }
}

void HSD_GObjProc_RemoveAllProcs(HSD_GObj* gobj)
{
    HSD_GObjProc* process = gobj->proc;
    while (process != NULL) {
        HSD_GObjProc* next_owned_process = process->child;
        HSD_GObjProc_RemoveProc(process);
        process = next_owned_process;
    }
}
