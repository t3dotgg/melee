#ifndef _DOLPHIN_CARDWRITE_H_
#define _DOLPHIN_CARDWRITE_H_

#include <dolphin/types.h>

s32 CARDWriteAsync(struct CARDFileInfo * fileInfo, void * buf, s32 length, s32 offset, void (* callback)(s32, s32));
s32 CARDWrite(struct CARDFileInfo * fileInfo, void * buf, s32 length, s32 offset);

#endif // _DOLPHIN_CARDWRITE_H_
