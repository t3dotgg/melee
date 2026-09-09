#include <dolphin/thp/thp.h>

#include <dolphin/os.h>

/* Movie playback is outside the first native runtime slice. Keep the API
 * explicit so game code can detect that a THP stream is unavailable. */
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work)
{
    (void) file;
    (void) tileY;
    (void) tileU;
    (void) tileV;
    (void) work;
    return -1;
}

s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out)
{
    (void) data;
    if (out != NULL) {
        *out = (THPDec_8032FD40_Data) { 0 };
    }
    return 0;
}

s32 THPDec_8032FD40(THPDec_8032FD40_Data* data, u16 height)
{
    (void) data;
    (void) height;
    return 0;
}

void THPDec_80331340(s32 decoded, void* y, void* u, void* v)
{
    (void) decoded;
    (void) y;
    (void) u;
    (void) v;
}

void THPDec_803313D0(s32 decoded, void* y, void* u, void* v, u32 stride)
{
    (void) decoded;
    (void) y;
    (void) u;
    (void) v;
    (void) stride;
}

void THPInit(void) {}
