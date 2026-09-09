#include <dolphin/card.h>

/*
 * The native host has no GameCube memory-card device. Keep the CARD ABI
 * available so game code can build and report the normal "no card" state.
 *
 * These functions do not touch the supplied buffers or call asynchronous
 * callbacks. A future host save backend can replace this file without
 * changing callers.
 */

static s32 card_no_card(void)
{
    return CARD_RESULT_NOCARD;
}

void CARDInit(void)
{
}

s32 CARDGetResultCode(s32 chan)
{
    (void) chan;
    return CARD_RESULT_NOCARD;
}

int CARDProbe(s32 chan)
{
    (void) chan;
    /* CARDProbe reports a boolean, unlike the result-code APIs. */
    return 0;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    (void) chan;
    if (memSize != NULL) {
        *memSize = 0;
    }
    if (sectorSize != NULL) {
        *sectorSize = 0;
    }
    return card_no_card();
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback)
{
    (void) chan;
    (void) workArea;
    (void) detachCallback;
    (void) attachCallback;
    return card_no_card();
}

s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback)
{
    (void) chan;
    (void) workArea;
    (void) detachCallback;
    return card_no_card();
}

s32 CARDUnmount(s32 chan)
{
    (void) chan;
    return card_no_card();
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback)
{
    (void) chan;
    (void) callback;
    return card_no_card();
}

s32 CARDCheck(s32 chan)
{
    (void) chan;
    return card_no_card();
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)
{
    (void) chan;
    if (byteNotUsed != NULL) {
        *byteNotUsed = 0;
    }
    if (filesNotUsed != NULL) {
        *filesNotUsed = 0;
    }
    return card_no_card();
}

s32 CARDOpen(s32 chan, char* fileName, CARDFileInfo* fileInfo)
{
    (void) chan;
    (void) fileName;
    (void) fileInfo;
    return card_no_card();
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo)
{
    (void) chan;
    (void) fileNo;
    (void) fileInfo;
    return card_no_card();
}

s32 CARDClose(CARDFileInfo* fileInfo)
{
    (void) fileInfo;
    return card_no_card();
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size,
                    CARDFileInfo* fileInfo, CARDCallback callback)
{
    (void) chan;
    (void) fileName;
    (void) size;
    (void) fileInfo;
    (void) callback;
    return card_no_card();
}

s32 CARDCreate(s32 chan, char* fileName, u32 size, CARDFileInfo* fileInfo)
{
    (void) chan;
    (void) fileName;
    (void) size;
    (void) fileInfo;
    return card_no_card();
}

s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback)
{
    (void) chan;
    (void) fileName;
    (void) callback;
    return card_no_card();
}

s32 CARDDelete(s32 chan, char* fileName)
{
    (void) chan;
    (void) fileName;
    return card_no_card();
}

s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback)
{
    (void) chan;
    (void) fileNo;
    (void) callback;
    return card_no_card();
}

s32 CARDFastDelete(s32 chan, s32 fileNo)
{
    (void) chan;
    (void) fileNo;
    return card_no_card();
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                  CARDCallback callback)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    (void) callback;
    return card_no_card();
}

s32 CARDRead(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    return card_no_card();
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                   CARDCallback callback)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    (void) callback;
    return card_no_card();
}

s32 CARDWrite(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    return card_no_card();
}

s32 CARDCancel(CARDFileInfo* fileInfo)
{
    (void) fileInfo;
    return card_no_card();
}

s32 CARDGetXferredBytes(s32 chan)
{
    (void) chan;
    return 0;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    (void) chan;
    (void) fileNo;
    (void) stat;
    return card_no_card();
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat,
                       CARDCallback callback)
{
    (void) chan;
    (void) fileNo;
    (void) stat;
    (void) callback;
    return card_no_card();
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    (void) chan;
    (void) fileNo;
    (void) stat;
    return card_no_card();
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName,
                    CARDCallback callback)
{
    (void) chan;
    (void) oldName;
    (void) newName;
    (void) callback;
    return card_no_card();
}

s32 CARDRename(s32 chan, char* oldName, char* newName)
{
    (void) chan;
    (void) oldName;
    (void) newName;
    return card_no_card();
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    (void) chan;
    (void) callback;
    return card_no_card();
}

s32 CARDFormat(s32 chan)
{
    (void) chan;
    return card_no_card();
}

s32 CARDGetEncoding(s32 chan, unsigned short* encode)
{
    (void) chan;
    (void) encode;
    return card_no_card();
}

s32 CARDGetMemSize(s32 chan, unsigned short* size)
{
    (void) chan;
    (void) size;
    return card_no_card();
}

s32 CARDGetSectorSize(s32 chan, u32* size)
{
    (void) chan;
    (void) size;
    return card_no_card();
}
