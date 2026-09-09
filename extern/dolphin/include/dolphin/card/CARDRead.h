#ifndef _DOLPHIN_CARDREAD_H_
#define _DOLPHIN_CARDREAD_H_

#include <dolphin/types.h>

s32 CARDReadAsync(CARDFileInfo *fileInfo, void *buf, s32 length, s32 offset, CARDCallback callback);
s32 CARDRead(struct CARDFileInfo * fileInfo, void * buf, s32 length, s32 offset);
s32 CARDCancel(CARDFileInfo *fileInfo);

#endif // _DOLPHIN_CARDREAD_H_
