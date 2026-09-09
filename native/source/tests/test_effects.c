#include <assert.h>
#undef __assert
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <melee/ef/eflib.h>

int main(void)
{
    void* objects[9];
    unsigned int i;
    for (i = 0; i < 9; i++) {
        objects[i] = malloc(16);
        assert(objects[i] != NULL);
        assert((uintptr_t) objects[i] > UINT32_MAX);
    }
    for (i = 0; i < 32; i++) {
        efLib_AnimQueue[i] = objects[i % 9];
    }
    memset(efLib_ParamTable, 0, sizeof(efLib_ParamTable));
    for (i = 0; i < 8; i++) {
        efLib_SetParamAlpha(objects[i], 10 + i);
        efLib_SetParamGfxId(objects[i], 100 + i);
        assert(efLib_ParamTable[i].gobj == objects[i]);
        assert(efLib_ParamTable[i].alpha == 10 + i);
        assert(efLib_ParamTable[i].gfx_id == 100 + i);
    }
    efLib_SetParamAlpha(objects[3], 200);
    assert(efLib_ParamTable[3].alpha == 200);
    efLib_SetParamAlpha(objects[8], 201);
    for (i = 0; i < 8; i++) {
        assert(efLib_ParamTable[i].gobj == objects[i]);
    }
    efLib_ParamTable[4].gobj = NULL;
    efLib_SetParamGfxId(objects[8], 999);
    assert(efLib_ParamTable[4].gobj == objects[8]);
    assert(efLib_ParamTable[4].gfx_id == 999);
    for (i = 0; i < 32; i++) {
        assert(efLib_AnimQueue[i] == objects[i % 9]);
    }
    for (i = 0; i < 9; i++) {
        free(objects[i]);
    }
    puts("Native effect parameter test passed with pointers above 4 GiB");
    return 0;
}
