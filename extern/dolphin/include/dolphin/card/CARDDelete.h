#ifndef _DOLPHIN_CARDDELETE_H_
#define _DOLPHIN_CARDDELETE_H_

#include <dolphin/types.h>

s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback);
s32 CARDFastDelete(s32 chan, s32 fileNo);
s32 CARDDeleteAsync(s32 chan, char *fileName, CARDCallback callback);
s32 CARDDelete(s32 chan, char *fileName);

#endif // _DOLPHIN_CARDDELETE_H_
