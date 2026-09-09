#include <assert.h>
#undef __assert
#include <stdint.h>
#include <stdio.h>

#include <sysdolphin/baselib/jobj.h>

int main(void)
{
    void* original = (void*) ((uintptr_t) 1 << 32 | 0x1234);
    void* tagged = HSD_JObjNativeParticleActivate(original);

    assert(HSD_JObjNativeParticleIsActive(tagged));
    assert(HSD_JObjNativeParticleClear(tagged) == original);
    assert((uintptr_t) HSD_JObjNativeParticleClear(tagged) > UINT32_MAX);
    puts("Native particle tag preserved the full host pointer");
    return 0;
}
