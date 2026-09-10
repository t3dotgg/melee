#include <dolphin/ar.h>

#ifdef MELEE_NATIVE
#include <string.h>
static u32 g_chunk_size = 0x1000;
static int g_initialized;
void ARQInit(void)
{
    g_initialized = 1;
}
void ARQReset(void)
{
    g_initialized = 0;
}
void ARQPostRequest(ARQRequest* request, uptr owner, u32 type, u32 priority,
                    uptr source, uptr dest, u32 length, ARQCallback callback)
{
    (void) priority;
    if (request == NULL) {
        return;
    }
    if (!g_initialized) {
        ARQInit();
    }
    request->next = NULL;
    request->owner = owner;
    request->type = type;
    request->priority = priority;
    request->source = source;
    request->dest = dest;
    request->length = length;
    request->callback = callback;
    if (type == ARAM_DIR_MRAM_TO_ARAM) {
        ARStartDMA(type, source, (u32) dest, length);
    } else {
        ARStartDMA(type, dest, (u32) source, length);
    }
    if (callback != NULL) {
        callback(request);
    }
}
void ARQRemoveRequest(ARQRequest* request)
{
    (void) request;
}
void ARQRemoveOwnerRequest(uptr owner)
{
    (void) owner;
}
void ARQFlushQueue(void) {}
void ARQSetChunkSize(u32 size)
{
    if (size != 0) {
        g_chunk_size = size;
    }
}
u32 ARQGetChunkSize(void)
{
    return g_chunk_size;
}
#endif
