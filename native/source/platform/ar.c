#include <dolphin/ar.h>

#ifdef MELEE_NATIVE
#include <stdlib.h>
#include <string.h>

/* Host audio keeps all decoded SSM voices resident. A 32 MiB ARAM model
 * leaves room for those voices and the game heaps that follow them. */
#define NATIVE_ARAM_SIZE (32u * 1024u * 1024u)
static unsigned char* g_aram;
static u32 g_aram_top;
static u32 g_aram_limit = NATIVE_ARAM_SIZE;
static u32* g_alloc_lengths;
static u32 g_alloc_count;
static ARQCallback g_dma_callback;

ARQCallback ARRegisterDMACallback(ARQCallback callback)
{
    ARQCallback old = g_dma_callback;
    g_dma_callback = callback;
    return old;
}

u32 ARGetDMAStatus(void)
{
    return 0;
}

void ARStartDMA(u32 type, uptr mainmem_addr, u32 aram_addr, u32 length)
{
    if (g_aram == NULL) {
        ARInit(NULL, 0);
    }
    if (aram_addr >= g_aram_limit) {
        return;
    }
    if (length > g_aram_limit - aram_addr) {
        length = g_aram_limit - aram_addr;
    }
    void* memory = (void*) mainmem_addr;
    if (type == ARAM_DIR_MRAM_TO_ARAM) {
        memcpy(g_aram + aram_addr, memory, length);
    } else {
        memcpy(memory, g_aram + aram_addr, length);
    }
    if (g_dma_callback != NULL) {
        g_dma_callback(NULL);
    }
}

u32 ARAlloc(u32 length)
{
    if (g_aram == NULL) {
        ARInit(NULL, 0);
    }
    length = (length + 31u) & ~31u;
    if (length > g_aram_limit - g_aram_top) {
        return 0;
    }
    u32 result = g_aram_top;
    g_aram_top += length;
    if (g_alloc_lengths != NULL) {
        g_alloc_lengths[g_alloc_count++] = length;
    }
    return result;
}

u32 ARFree(u32* length)
{
    if (g_alloc_count == 0) {
        return g_aram_top;
    }
    u32 amount = g_alloc_lengths[--g_alloc_count];
    if (length != NULL) {
        *length = amount;
    }
    g_aram_top -= amount;
    return g_aram_top;
}

int ARCheckInit(void)
{
    return g_aram != NULL;
}

u32 ARInit(u32* stack_index_addr, u32 num_entries)
{
    (void) stack_index_addr;
    if (g_aram == NULL) {
        g_aram = calloc(1, NATIVE_ARAM_SIZE);
    }
    if (g_alloc_lengths == NULL) {
        g_alloc_lengths = calloc(num_entries > 0 ? num_entries : 4096,
                                 sizeof(*g_alloc_lengths));
    }
    g_aram_top = 0x4000;
    g_alloc_count = 0;
    return g_aram_top;
}

void ARReset(void)
{
    free(g_aram);
    g_aram = NULL;
    free(g_alloc_lengths);
    g_alloc_lengths = NULL;
    g_aram_top = 0;
    g_alloc_count = 0;
}
void ARSetSize(void) {}
u32 ARGetBaseAddress(void)
{
    return 0x4000;
}
u32 ARGetSize(void)
{
    return NATIVE_ARAM_SIZE;
}

/* AX voice addresses are offsets into the native ARAM byte buffer. */
const unsigned char* NativeARAMPointer(u32 address, u32 length)
{
    if (g_aram == NULL || address > g_aram_limit ||
        length > g_aram_limit - address)
    {
        return NULL;
    }
    return g_aram + address;
}

#endif
