#include "devcom.h"

#include "debug.h"
#include "devcom.static.h"
#include "synth.h"

#ifdef MELEE_NATIVE
/*
 * The host DVD and ARQ backends complete requests inline. GameCube DVD
 * callbacks run later, so the original code can call HSD_DevComDVDWakeUp
 * from a callback without growing the stack. Defer nested wakeups on the
 * host and drain them from one outer loop instead.
 */
static bool native_dvd_wakeup_active;
static bool native_dvd_wakeup_pending;
#endif

bool HSD_DevComIsBusy(int idx)
{
    return (bool) devComStatus[idx];
}

static void HSD_DevComUnlink(HSD_DevCom* dc)
{
    HSD_DevCom* curr;
    bool enabled = OSDisableInterrupts();
    int i = dc->dcReq & 3;

    if (devComStatus[i] == dc) {
        devComStatus[i] = dc->next;
        if (HSD_DevCom_804C6330[i] == dc) {
            HSD_DevCom_804C6330[i] = 0;
        }
        goto cleanup;
    }

    for (curr = devComStatus[i]; curr->next != NULL; curr = curr->next) {
        if (curr->next == dc) {
            curr->next = dc->next;
            if (HSD_DevCom_804C6330[i] == dc) {
                HSD_DevCom_804C6330[i] = curr;
            }
            OSRestoreInterrupts(enabled);
            return;
        }
    }
    HSD_ASSERT(0x6E, 0);

cleanup:
    OSRestoreInterrupts(enabled);
}

static void HSD_DevComStdCallback(ARQRequest* request)
{
    int i;

    if (request == &devComARQR[0][0]) {
        i = 0;
    } else if (request == &devComARQR[1][0]) {
        i = 1;
    } else {
        HSD_ASSERT(0xA5, 0);
    }
    aramstate = 0;
    devComRelayBufFlag[i] = false;
    HSD_DevComDVDWakeUp();
    HSD_DevComARAMWakeUp();
}

static inline void HSD_DevComARAMCallback_inline(HSD_DevCom* devcom)
{
    bool enabled = OSDisableInterrupts();
    devcom->next = HSD_DevCom_804D77F0;
    HSD_DevCom_804D77F0 = devcom;
    OSRestoreInterrupts(enabled);
}

static void HSD_DevComARAMCallback(ARQRequest* request)
{
    int i;
    void* buf;

    if (aramDC->type == 0x1A) {
        if (request == &devComARQR[0][0]) {
            i = 0;
        } else {
            i = 1;
        }
        buf = HSD_DevCom_804C6330_bufs[i];
    } else {
        buf = NULL;
    }

    if (aramDC->callback != NULL) {
        aramDC->callback(aramDC->dcReq, (intptr_t) aramDC->args, buf,
                         aramDC->cancelflag);
    }

    HSD_DevComUnlink(aramDC);
    HSD_DevComARAMCallback_inline(aramDC);
    HSD_DevComStdCallback(request);
}

static inline int getRelayBufIdx(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (!devComRelayBufFlag[i]) {
            devComRelayBufFlag[i] = true;
            return i;
        }
    }
    return -1;
}

void HSD_DevComARAMWakeUp(void)
{
    bool enabled;
    int req_idx;
    u32 xfer_size2;
    void (*arq_callback)(ARQRequest*);
    void (*arq_callback2)(ARQRequest*);

    enabled = OSDisableInterrupts();
    if (aramstate != 0) {
        OSRestoreInterrupts(enabled);
        return;
    }
    aramDC = devComStatus[3];
    if (devComStatus[3] != NULL) {
        if (aramDC->cancelflag) {
            if (aramDC->callback != NULL) {
                aramDC->callback(aramDC->dcReq, (intptr_t) aramDC->args, NULL,
                                 true);
            }
            HSD_DevComUnlink(aramDC);
            OSRestoreInterrupts(enabled);
            HSD_DevComARAMWakeUp();
            return;
        }
        req_idx = getRelayBufIdx();
        if (req_idx >= 0) {
            if (aramDC->type == 3) {
                u32 xfer_size;
                uintptr_t dest;
                if (aramDC->size > DEVCOM_BUF_SIZE) {
                    arq_callback = HSD_DevComStdCallback;
                    xfer_size = DEVCOM_BUF_SIZE;
                } else {
                    arq_callback = HSD_DevComARAMCallback;
                    xfer_size = aramDC->size;
                }
                {
                    int* p = HSD_DevCom_804C6330_bufs[req_idx];
                    int i;
                    for (i = 0x1000; i > 0; i--) {
                        *p++ = 0;
                    }
                }
                DCStoreRange(HSD_DevCom_804C6330_bufs[req_idx],
                             DEVCOM_BUF_SIZE);
                /*
                 * The native ARQ backend can invoke the callback before
                 * ARQPostRequest returns. Publish the in-flight state and
                 * advance the request before posting so a callback that
                 * wakes ARAM again sees the next chunk.
                 */
                dest = aramDC->dest;
                aramDC->dest += xfer_size;
                aramDC->size -= xfer_size;
                aramstate = 1;
                ARQPostRequest(devComARQR[req_idx], 0, 0, 1,
                               (uintptr_t) HSD_DevCom_804C6330_bufs[req_idx],
                               dest, xfer_size, arq_callback);
            } else if (aramDC->type == 0xB) {
                DCStoreRange((void*) aramDC->src, aramDC->size);
                aramstate = 1;
                ARQPostRequest(devComARQR[req_idx], 0, 0, 1, aramDC->src,
                               aramDC->dest, aramDC->size,
                               HSD_DevComARAMCallback);
            } else if (aramDC->type == 0x19) {
                DCInvalidateRange((void*) aramDC->dest, aramDC->size);
                aramstate = 1;
                ARQPostRequest(devComARQR[req_idx], 0, 1, 1, aramDC->src,
                               aramDC->dest, aramDC->size,
                               HSD_DevComARAMCallback);
            } else if (aramDC->type == 0x1A) {
                DCInvalidateRange(HSD_DevCom_804C6330_bufs[req_idx],
                                  DEVCOM_BUF_SIZE);
                aramstate = 1;
                ARQPostRequest(devComARQR[req_idx], 0, 1, 1, aramDC->src,
                               (uintptr_t) HSD_DevCom_804C6330_bufs[req_idx],
                               aramDC->size, HSD_DevComARAMCallback);
            } else if (aramDC->type == 0x1B) {
                uintptr_t src;
                uintptr_t dest;
                DCInvalidateRange(HSD_DevCom_804C6330_bufs[req_idx],
                                  DEVCOM_BUF_SIZE);
                if (aramDC->size > DEVCOM_BUF_SIZE) {
                    arq_callback2 = HSD_DevComStdCallback;
                    xfer_size2 = DEVCOM_BUF_SIZE;
                } else {
                    arq_callback2 = HSD_DevComARAMCallback;
                    xfer_size2 = aramDC->size;
                }
                aramstate = 1;
                src = aramDC->src;
                dest = aramDC->dest;
                ARQPostRequest(&devComARQR[req_idx][1], 0, 1, 1, src,
                               (uintptr_t) HSD_DevCom_804C6330_bufs[req_idx],
                               xfer_size2, NULL);
                aramDC->src += xfer_size2;
                aramDC->dest += xfer_size2;
                aramDC->size -= xfer_size2;
                ARQPostRequest(&devComARQR[req_idx][0], 0, 0, 1,
                               (uintptr_t) HSD_DevCom_804C6330_bufs[req_idx],
                               dest, xfer_size2, arq_callback2);
            }
        }
    }
    OSRestoreInterrupts(enabled);
}

static void HSD_DevComDVDStdCallback(ARQRequest* request)
{
    int i;
    if (request == &devComARQR[0][0]) {
        i = 0;
    } else if (request == &devComARQR[1][0]) {
        i = 1;
    } else {
        HSD_ASSERT(0x158, 0);
    }
    devComRelayBufFlag[i] = false;
    HSD_DevComDVDWakeUp();
    HSD_DevComARAMWakeUp();
}

static void HSD_DevComDVDARAMEndCallback(ARQRequest* request)
{
    int i;

    HSD_DevComDVDStdCallback(request);

    if (request == &devComARQR[0][0]) {
        i = 0;
    } else {
        i = 1;
    }

#ifdef MELEE_NATIVE
    /* Keep the request on the DVD queue until HSD_DevComDVDCallback unlinks
     * it. Adding it to the free list here would overwrite its queue link while
     * the synchronous native ARQ callback is still unwinding. */
    if (HSD_DevCom_804D77FC[i]->callback != NULL && HSD_DevCom_804D7804 == 0) {
        HSD_DevCom_804D77FC[i]->callback(
            HSD_DevCom_804D77FC[i]->dcReq,
            (intptr_t) HSD_DevCom_804D77FC[i]->args, NULL,
            HSD_DevCom_804D77FC[i]->cancelflag);
    }
    HSD_DevCom_804D77FC[i] = NULL;
#else
    if (HSD_DevCom_804D77FC[i]->callback != NULL && HSD_DevCom_804D7804 == 0) {
        HSD_DevCom_804D77FC[i]->callback(
            HSD_DevCom_804D77FC[i]->dcReq,
            (intptr_t) HSD_DevCom_804D77FC[i]->args, NULL,
            HSD_DevCom_804D77FC[i]->cancelflag);
    }
    HSD_DevComARAMCallback_inline(HSD_DevCom_804D77FC[i]);
    HSD_DevCom_804D77FC[i] = NULL;
#endif
}

static void HSD_DevComDVDMemCallback(s32 result, DVDFileInfo* unused)
{
    HSD_DevCom* dc;
    HSD_DevCom* active_dc = dvdDC;
    bool enabled;

    if (result == -1) {
        HSD_DevCom_804D7804 = 1;
    }
    if (active_dc->size > 0x80000) {
        active_dc->src += 0x80000;
        active_dc->dest += 0x80000;
        active_dc->size -= 0x80000;
        HSD_DevCom_804D77F5 = 0;
        HSD_DevComDVDWakeUp();
        return;
    }
#ifdef MELEE_NATIVE
    /* Remove the completed request before calling a client. Native callbacks
     * run inline and can submit a replacement request on the same channel. */
    HSD_DevComUnlink(active_dc);
    dc = active_dc;
    if (active_dc->callback != NULL && HSD_DevCom_804D7804 == 0) {
        active_dc->callback(active_dc->dcReq, (intptr_t) active_dc->args, NULL,
                            active_dc->cancelflag);
    }
#else
    if (active_dc->callback != NULL && HSD_DevCom_804D7804 == 0) {
        active_dc->callback(active_dc->dcReq, (intptr_t) active_dc->args, NULL,
                            active_dc->cancelflag);
    }
    dvdDC = active_dc;
    HSD_DevComUnlink(active_dc);
    dc = active_dc;
#endif
    enabled = OSDisableInterrupts();
    dc->next = HSD_DevCom_804D77F0;
    HSD_DevCom_804D77F0 = dc;
    OSRestoreInterrupts(enabled);
    HSD_DevCom_804D77F5 = 0;
    HSD_DevComDVDWakeUp();
}

static void HSD_DevComDVDCallback(s32 result, DVDFileInfo* unused)
{
    HSD_DevCom* dc;
    HSD_DevCom* active_dc = dvdDC;
    s32 enabled;
    u16 type;

    PAD_STACK(8);

    if (result == -1) {
        HSD_DevCom_804D7804 = 1;
    }
    type = dvdDC->type;
    if (type == 0x22) {
        HSD_ASSERT(0x18C, active_dc->size <= DEVCOM_BUF_SIZE);
        HSD_ASSERT(0x18D, active_dc->callback);
        if (HSD_DevCom_804D7804 == 0) {
            active_dc->callback(active_dc->dcReq, (intptr_t) active_dc->args,
                            HSD_DevCom_804C6330_bufs[HSD_DevCom_804D77F6],
                            active_dc->cancelflag);
        }
        dvdDC = active_dc;
        HSD_DevComUnlink(active_dc);
        dc = active_dc;
        enabled = OSDisableInterrupts();
        dc->next = HSD_DevCom_804D77F0;
        HSD_DevCom_804D77F0 = dc;
        OSRestoreInterrupts(enabled);
        HSD_DevCom_804D77F5 = 0;
        devComRelayBufFlag[HSD_DevCom_804D77F6] = false;
        HSD_DevComDVDWakeUp();
        HSD_DevComARAMWakeUp();
    } else if (type == 0x23) {
        HSD_DevCom_804D77F7 = HSD_DevCom_804D77F6;
        if (active_dc->size > DEVCOM_BUF_SIZE) {
            ARQPostRequest(
                devComARQR[HSD_DevCom_804D77F7], 0, 0, 1,
                (uintptr_t) HSD_DevCom_804C6330_bufs[HSD_DevCom_804D77F7],
                active_dc->dest, DEVCOM_BUF_SIZE, HSD_DevComDVDStdCallback);
            active_dc->src += DEVCOM_BUF_SIZE;
            active_dc->dest += DEVCOM_BUF_SIZE;
            active_dc->size -= DEVCOM_BUF_SIZE;
            HSD_DevCom_804D77F5 = 0;
            HSD_DevComDVDWakeUp();
        } else {
            HSD_DevCom_804D77FC[HSD_DevCom_804D77F7] = active_dc;
            ARQPostRequest(
                devComARQR[HSD_DevCom_804D77F7], 0, 0, 1,
                (uintptr_t) HSD_DevCom_804C6330_bufs[HSD_DevCom_804D77F7],
                active_dc->dest, active_dc->size, HSD_DevComDVDARAMEndCallback);
            HSD_DevComUnlink(active_dc);
#ifdef MELEE_NATIVE
            enabled = OSDisableInterrupts();
            active_dc->next = HSD_DevCom_804D77F0;
            HSD_DevCom_804D77F0 = active_dc;
            OSRestoreInterrupts(enabled);
#endif
            HSD_DevCom_804D77F5 = 0;
            HSD_DevComDVDWakeUp();
        }
    }
}

#ifdef MELEE_NATIVE
static void HSD_DevComDVDWakeUpImpl(void)
#else
void HSD_DevComDVDWakeUp(void)
#endif
{
    bool enabled = OSDisableInterrupts();
    int i;
    int buf_idx;

    if (HSD_DevCom_804D77F5 != 0) {
        OSRestoreInterrupts(enabled);
        return;
    }
    for (i = 0; i < 3; i++) {
        if ((dvdDC = devComStatus[i])) {
            if (dvdDC->cancelflag) {
                if (dvdDC->callback != NULL) {
                    dvdDC->callback(dvdDC->dcReq, (intptr_t) dvdDC->args, NULL,
                                    true);
                }
                HSD_DevComUnlink(dvdDC);
                OSRestoreInterrupts(enabled);
                HSD_DevComDVDWakeUp();
                return;
            }
            DVDFastOpen(dvdDC->file, &fileinfo);
            if (dvdDC->type == 0x21) {
                /* Native DVD reads complete inline. Mark the channel busy
                 * before calling into the backend so its callback cannot
                 * re-enter this wakeup path as a second request. */
                HSD_DevCom_804D77F5 = 1;
                if (!DVDReadAsyncPrio(&fileinfo, (void*) dvdDC->dest,
                                      MIN(dvdDC->size, 0x80000),
                                      (s32) dvdDC->src,
                                      HSD_DevComDVDMemCallback, 2)) {
                    HSD_DevCom_804D77F5 = 0;
                }
                OSRestoreInterrupts(enabled);
                return;
            }
            buf_idx = getRelayBufIdx();
            if (buf_idx >= 0) {
                HSD_DevCom_804D77F6 = buf_idx;
                HSD_DevCom_804D77F5 = 1;
                if (!DVDReadAsyncPrio(&fileinfo,
                                      HSD_DevCom_804C6330_bufs[buf_idx],
                                      MIN(dvdDC->size, DEVCOM_BUF_SIZE),
                                      dvdDC->src, HSD_DevComDVDCallback, 2)) {
                    HSD_DevCom_804D77F5 = 0;
                }
                OSRestoreInterrupts(enabled);
                return;
            }
        }
    }
    OSRestoreInterrupts(enabled);
}

#ifdef MELEE_NATIVE
void HSD_DevComDVDWakeUp(void)
{
    if (native_dvd_wakeup_active) {
        native_dvd_wakeup_pending = true;
        return;
    }

    native_dvd_wakeup_active = true;
    do {
        native_dvd_wakeup_pending = false;
        HSD_DevComDVDWakeUpImpl();
    } while (native_dvd_wakeup_pending);
    native_dvd_wakeup_active = false;
}
#endif

static inline int HSD_DevComGetDestType(int type)
{
    return type & 7;
}

#define INIT_N_DEVCOMS 16

static inline void DevComLinkNext(HSD_DevCom* dc)
{
    int i;
    for (i = 1; i < INIT_N_DEVCOMS - 1; i++) {
        dc[i].next = &dc[i] + 1;
    }
    dc[i].next = NULL;
}

int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size,
                      int type, int pri, HSD_DevComCallback cb, void* args)
{
    bool enabled;
    HSD_DevCom* dc;
    int result;

    enabled = OSDisableInterrupts();

    if ((dc = HSD_DevCom_804D77F0)) {
        HSD_DevCom_804D77F0 = dc->next;
        OSRestoreInterrupts(enabled);
    } else {
        dc = HSD_AudioMalloc(sizeof(HSD_DevCom) * INIT_N_DEVCOMS);
        DevComLinkNext(dc);

        HSD_DevCom_804D77F0 = &dc[1];
        OSRestoreInterrupts(enabled);
    }

    HSD_ASSERT(0x1ED, dc);
    HSD_ASSERT(0x1EE,
        !(HSD_DevComGetDestType(type) == DEVCOMDEST_SBUF
            && size > DEVCOM_BUF_SIZE));

#ifndef MELEE_NATIVE
    HSD_ASSERT(0x1EF, src % 32 == 0);
    HSD_ASSERT(0x1F0, dest % 32 == 0);
    HSD_ASSERT(0x1F1, size % 32 == 0);
    HSD_ASSERT(0x1F2, size != 0);
#else
    /* Host buffers do not need GameCube cache-line alignment. */
    if (size == 0) {
        HSD_AudioFree(dc);
        return -1;
    }
#endif

    pri = (type & 0x38) == 0x20 ? pri : 3;

    dc->file = file;
    dc->src = src;
    dc->dest = dest;
    dc->size = size;
    dc->type = type;
    dc->cancelflag = false;
    dc->callback = cb;
    dc->args = args;

    enabled = OSDisableInterrupts();
    result = HSD_DevCom_804D6050 + pri;
    dc->dcReq = result;
    HSD_DevCom_804D6050 += 4;
    if (HSD_DevCom_804C6330[pri] != NULL) {
        HSD_DevCom_804C6330[pri]->next = dc;
    }
    HSD_DevCom_804C6330[pri] = dc;
    dc->next = NULL;
    if (devComStatus[pri] == NULL) {
        devComStatus[pri] = dc;
        HSD_DevComDVDWakeUp();
        HSD_DevComARAMWakeUp();
    }
    OSRestoreInterrupts(enabled);

    return result;
}

static inline HSD_DevCom* HSD_DevComCancelEx_inline(int dcReq)
{
    HSD_DevCom* cur = devComStatus[dcReq & 3];
    while (cur != NULL) {
        if (cur->dcReq == dcReq) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

int HSD_DevComCancelEx(int dcReq, u32 flags, HSD_DevComCallback cb, void* args)
{
    HSD_DevCom* dc;
    bool enabled = OSDisableInterrupts();

    if ((dc = HSD_DevComCancelEx_inline(dcReq))) {
        int tmp = dcReq & 3;
        if (flags & 1) {
            dc->callback = cb;
        }
        if (flags & 2) {
            dc->args = args;
        }
        if (devComStatus[tmp] == dc) {
            dc->cancelflag = true;
        } else {
            if (dc->callback != NULL) {
                dc->callback(dc->dcReq, (intptr_t) dc->args, NULL, true);
            }
            HSD_DevComUnlink(dc);
        }
    } else {
        int i;
        for (i = 0; i < 2; i++) {
            HSD_DevCom* dc = HSD_DevCom_804D77FC[i];
            if (dc != NULL && dc->dcReq == dcReq) {
                dc->callback = cb;
                dc->args = args;
                HSD_DevCom_804D77FC[i]->cancelflag = true;
            }
        }
    }
    OSRestoreInterrupts(enabled);
    return 0;
}
