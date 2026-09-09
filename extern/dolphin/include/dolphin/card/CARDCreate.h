#ifndef _DOLPHIN_CARDCREATE_H_
#define _DOLPHIN_CARDCREATE_H_

#include <dolphin/types.h>

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback);
s32 CARDCreate(s32 chan, char * fileName, u32 size, struct CARDFileInfo * fileInfo);

#endif // _DOLPHIN_CARDCREATE_H_
