#ifndef SYSDOLPHIN_BASELIB_GOBJPLINK_H
#define SYSDOLPHIN_BASELIB_GOBJPLINK_H

#include <Runtime/platform.h>

#include <sysdolphin/baselib/forward.h>

#include <sysdolphin/baselib/gobj.h>

// Insert an unlinked object after predecessor, or at the head if NULL.
void GObj_PReorder(HSD_GObj* gobj, HSD_GObj* predecessor);

// where: 0 after equal priorities, 1 before them, 2 after position, 3 before
// it.
HSD_GObj* CreateGObj(s32 where, u16 classifier, u8 p_link, u8 priority,
                     HSD_GObj* position);

// Remove the owner and its attachments, or defer until its callback returns.
void HSD_GObjFree(HSD_GObj* gobj);

// Move the owner and reinsert its processes. Uses CreateGObj's placement
// modes.
void HSD_GObjPLink_ChangeGObjPri_Unk(u32 where, HSD_GObj* gobj, u8 p_link,
                                  u8 priority, HSD_GObj* position);

#endif
