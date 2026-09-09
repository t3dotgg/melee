#include "lbarq.h"

#include <placeholder.h>

#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/debug.h>

typedef enum lbArqState {
    LB_ARQ_STATE_FREE = 0,
    LB_ARQ_STATE_PENDING = 1,
    LB_ARQ_STATE_DONE = 2,
} lbArqState;

typedef struct lbArqNode {
    /* 0x00 */ struct lbArqNode* next;
    /* 0x04 */ lbArqState state;
    /* 0x08 */ ARQRequest arq;
    /* 0x28 */ lbArqCallback callback;
    /* 0x2C */ void* callback_arg;
} lbArqNode;

typedef struct lbArqGlobal {
    /* 0x000 */ lbArqNode nodes[10];
    /* 0x1E0 */ lbArqNode* list[3];
} lbArqGlobal;

/* 4316C0 */ lbArqGlobal lbArq_804316C0;

/// @todo Keep polling state reads out of line so MWCC reloads after DMA
///       interrupts. The original way of preventing inlining is not known.
#ifdef __MWERKS__
#pragma push
#pragma dont_inline on
#endif
static lbArqState lbArq_80014ABC(lbArqNode* node)
{
    return node->state;
}
#ifdef __MWERKS__
#pragma pop
#endif

static void lbArq_80014AC4(ARQRequest* request)
{
    lbArqGlobal* global = &lbArq_804316C0;
    lbArqNode* node = (lbArqNode*) request->owner;
    lbArqNode** node_link;
    lbArqNode** tail_link;
    uintptr_t offset;
    BOOL interrupts_enabled;

    interrupts_enabled = OSDisableInterrupts();

    /* Keep this addition order to match the original instructions. */
    offset = node->state * sizeof(global->list[0]);
    offset += offsetof(lbArqGlobal, list);
    offset += (uintptr_t) global;
    node_link = (lbArqNode**) offset;
    while (*node_link != node) {
        node_link = &(*node_link)->next;
    }
    *node_link = node->next;

    /* Publish completion before running the user callback. */
    tail_link = &global->list[LB_ARQ_STATE_DONE];
    while (*tail_link != NULL) {
        tail_link = &(*tail_link)->next;
    }
    *tail_link = node;
    node->next = NULL;
    node->state = LB_ARQ_STATE_DONE;

    OSRestoreInterrupts(interrupts_enabled);

    /* Recycle callback requests here. The blocking caller recycles its own. */
    if (node->callback != NULL) {
        node->callback(node->callback_arg);

        interrupts_enabled = OSDisableInterrupts();

        /* Find the link that points to this node, including the list head. */
        node_link = &global->list[node->state];
        while (*node_link != node) {
            node_link = &(*node_link)->next;
        }
        *node_link = node->next;

        /* Append to the free list so nodes are reused in queue order. */
        tail_link = &global->list[LB_ARQ_STATE_FREE];
        while (*tail_link != NULL) {
            tail_link = &(*tail_link)->next;
        }
        *tail_link = node;
        node->next = NULL;
        node->state = LB_ARQ_STATE_FREE;

        OSRestoreInterrupts(interrupts_enabled);
    }
}

void lbArq_80014BD0(unsigned int source, void* dest, size_t length,
                    lbArqCallback callback, void* callback_arg)
{
    u32 aram_source;
    lbArqNode* request_owner;
    lbArqGlobal* global = &lbArq_804316C0;
    lbArqNode* rp; /* Keep the name used by the original assertion. */
    lbArqNode** list_link;
    BOOL interrupts_enabled;
    lbArqNode** free_link;
    lbArqNode* free_node;

    PAD_STACK(16);
    DCInvalidateRange(dest, length);
    interrupts_enabled = OSDisableInterrupts();
    free_node = global->list[LB_ARQ_STATE_FREE];
    rp = free_node;
    free_link = &global->list[LB_ARQ_STATE_FREE];
    HSD_ASSERT(0x67, rp);
    *free_link = rp->next;
    rp->callback = callback;
    rp->callback_arg = callback_arg;

    /* Move the allocated node from the free list to the pending queue. */
    list_link = &global->list[LB_ARQ_STATE_PENDING];
    while (*list_link != NULL) {
        list_link = &(*list_link)->next;
    }
    *list_link = rp;
    rp->next = NULL;
    rp->state = LB_ARQ_STATE_PENDING;

    /* The DMA callback recovers this node from ARQRequest.owner. */
    request_owner = rp;
    aram_source = source;
    ARQPostRequest(&rp->arq, (uintptr_t) request_owner, ARQ_TYPE_ARAM_TO_MRAM,
                   ARQ_PRIORITY_LOW, aram_source, (uintptr_t) dest, length,
                   lbArq_80014AC4);

    /* Without a callback, wait with interrupts restored so DMA can finish. */
    if (rp->callback == NULL) {
        OSRestoreInterrupts(interrupts_enabled);
        while (lbArq_80014ABC(rp) != LB_ARQ_STATE_DONE) {
        }
        interrupts_enabled = OSDisableInterrupts();
        list_link = &global->list[rp->state];
        while (*list_link != rp) {
            list_link = &(*list_link)->next;
        }
        *list_link = rp->next;
        while (*free_link != NULL) {
            free_link = &(*free_link)->next;
        }
        *free_link = rp;
        rp->next = NULL;
        rp->state = LB_ARQ_STATE_FREE;
    }
    OSRestoreInterrupts(interrupts_enabled);
}

void lbArq_80014D2C(void)
{
    lbArqGlobal* global = &lbArq_804316C0;
    lbArqNode* nodes = global->nodes;
    lbArqNode* node;
    int i;

    global->list[LB_ARQ_STATE_FREE] = NULL;
    global->list[LB_ARQ_STATE_PENDING] = NULL;
    global->list[LB_ARQ_STATE_DONE] = NULL;
    global->list[LB_ARQ_STATE_FREE] = nodes;

    for (i = 0; i < 9; i++) {
        node = &nodes[i];
        node->next = node + 1;
        node->state = LB_ARQ_STATE_FREE;
    }
    node->next = NULL;
    node->state = LB_ARQ_STATE_FREE;

    PAD_STACK(8);
}
