#ifndef _DOLPHIN_CARDCHECK_H_
#define _DOLPHIN_CARDCHECK_H_

#include <dolphin/types.h>

s32 CARDCheckAsync(s32 chan, CARDCallback callback);
s32 CARDCheck(s32 chan);

#endif // _DOLPHIN_CARDCHECK_H_
