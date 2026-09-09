#include <stdatomic.h>

#include <dolphin/os/OSCache.h>

/* Native transfers use coherent host memory. The renderer owns GPU resource
 * synchronization. Keep the ordering required at a console DMA boundary. */
void DCFlushRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
    atomic_thread_fence(memory_order_seq_cst);
}

void DCStoreRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
    atomic_thread_fence(memory_order_release);
}

void DCInvalidateRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
    atomic_thread_fence(memory_order_acquire);
}
