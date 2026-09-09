#ifndef _DOLPHIN_DVD_H_
#define _DOLPHIN_DVD_H_

#include <dolphin/types.h>

typedef struct DVDDiskID
{
    char gameName[4];
    char company[2];
    u8 diskNumber;
    u8 gameVersion;
    u8 streaming;
    u8 streamingBufSize;
    u8 padding[22];
} DVDDiskID;

typedef struct DVDCommandBlock DVDCommandBlock;
typedef void (*DVDCBCallback)(s32 result, DVDCommandBlock *block);
struct DVDCommandBlock
{
    /*0x00*/ DVDCommandBlock *next;
    /*0x04*/ DVDCommandBlock *prev;
    /*0x08*/ u32 command;
    /*0x0C*/ s32 state;
    /*0x10*/ u32 offset;
    /*0x14*/ u32 length;
    /*0x18*/ void *addr;
    /*0x1C*/ u32 currTransferSize;
    /*0x20*/ u32 transferredSize;
    /*0x24*/ DVDDiskID *id;
    /*0x28*/ DVDCBCallback callback;
    /*0x2C*/ void *userData;
};

typedef struct DVDFileInfo  DVDFileInfo;
typedef void (*DVDCallback)(s32 result, DVDFileInfo *fileInfo);
struct DVDFileInfo
{
	/*0x00*/ DVDCommandBlock cb;
    /*0x30*/ u32 startAddr;
    /*0x34*/ u32 length;
    /*0x38*/ DVDCallback callback;
};

typedef struct
{
    u32 entryNum;
    u32 location;
    u32 next;
} DVDDir;

typedef struct 
{
    u32 entryNum;
    BOOL isDir;
    char *name;
} DVDDirEntry;

typedef struct DVDBB2 {
    /* 0x00 */ u32 bootFilePosition;
    /* 0x04 */ u32 FSTPosition;
    /* 0x08 */ u32 FSTLength;
    /* 0x0C */ u32 FSTMaxLength;
    /* 0x10 */ void * FSTAddress;
    /* 0x14 */ u32 userPosition;
    /* 0x18 */ u32 userLength;
    /* 0x1C */ u32 padding0;
} DVDBB2;

typedef struct DVDDriveInfo {
    /* 0x00 */ u16 revisionLevel;
    /* 0x02 */ u16 deviceCode;
    /* 0x04 */ u32 releaseDate;
    /* 0x08 */ u8 padding[24];
} DVDDriveInfo;

void DVDDumpWaitingQueue(void);
int DVDLowRead(void * addr, u32 length, u32 offset, void (* callback)(u32));
int DVDLowSeek(u32 offset, void (* callback)(u32));
int DVDLowWaitCoverClose(void (* callback)(u32));
int DVDLowReadDiskID(struct DVDDiskID * diskID, void (* callback)(u32));
int DVDLowStopMotor(void (* callback)(u32));
int DVDLowRequestError(void (* callback)(u32));
int DVDLowInquiry(struct DVDDriveInfo * info, void (* callback)(u32));
int DVDLowAudioStream(u32 subcmd, u32 length, u32 offset, void (* callback)(u32));
int DVDLowRequestAudioStatus(u32 subcmd, void (* callback)(u32));
int DVDLowAudioBufferConfig(int enable, u32 size, void (* callback)(u32));
void DVDLowReset();
void (* DVDLowSetResetCoverCallback(void (* callback)(u32)))(u32);
int DVDLowBreak();
void (* DVDLowClearCallback())(u32);
u32 DVDLowGetCoverStatus();

// dvd.c
void DVDInit();
int DVDReadAbsAsyncPrio(struct DVDCommandBlock * block, void * addr, s32 length, s32 offset, void (* callback)(s32, struct DVDCommandBlock *), s32 prio);
int DVDSeekAbsAsyncPrio(struct DVDCommandBlock * block, s32 offset, void (* callback)(s32, struct DVDCommandBlock *), s32 prio);
int DVDReadAbsAsyncForBS(struct DVDCommandBlock * block, void * addr, s32 length, s32 offset, void (* callback)(s32, struct DVDCommandBlock *));
int DVDReadDiskID(struct DVDCommandBlock * block, struct DVDDiskID * diskID, void (* callback)(s32, struct DVDCommandBlock *));
int DVDPrepareStreamAbsAsync(struct DVDCommandBlock * block, u32 length, u32 offset, void (* callback)(s32, struct DVDCommandBlock *));
int DVDCancelStreamAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDCancelStream(struct DVDCommandBlock * block);
int DVDStopStreamAtEndAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDStopStreamAtEnd(struct DVDCommandBlock * block);
int DVDGetStreamErrorStatusAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDGetStreamErrorStatus(struct DVDCommandBlock * block);
int DVDGetStreamPlayAddrAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDGetStreamPlayAddr(struct DVDCommandBlock * block);
int DVDGetStreamStartAddrAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDGetStreamStartAddr(struct DVDCommandBlock * block);
int DVDGetStreamLengthAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDGetStreamLength(struct DVDCommandBlock * block);
int DVDChangeDiskAsyncForBS(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
int DVDChangeDiskAsync(struct DVDCommandBlock * block, struct DVDDiskID * id, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDChangeDisk(struct DVDCommandBlock * block, struct DVDDiskID * id);
int DVDInquiryAsync(struct DVDCommandBlock * block, struct DVDDriveInfo * info, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDInquiry(struct DVDCommandBlock * block, struct DVDDriveInfo * info);
void DVDReset();
int DVDResetRequired();
s32 DVDGetCommandBlockStatus(struct DVDCommandBlock * block);
s32 DVDGetDriveStatus();
int DVDSetAutoInvalidation(int autoInval);
void DVDPause();
void DVDResume();
int DVDCancelAsync(struct DVDCommandBlock * block, void (* callback)(s32, struct DVDCommandBlock *));
s32 DVDCancel(volatile struct DVDCommandBlock * block);
int DVDCancelAllAsync(DVDCBCallback callback);
s32 DVDCancelAll(void);
struct DVDDiskID * DVDGetCurrentDiskID(void);
BOOL DVDCheckDisk(void);

// dvdfs.c
s32 DVDConvertPathToEntrynum(const char* pathPtr);
BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo);
BOOL DVDOpen(char* fileName, DVDFileInfo* fileInfo);
BOOL DVDClose(DVDFileInfo* fileInfo);
BOOL DVDGetCurrentDir(char* path, u32 maxlen);
BOOL DVDChangeDir(char* dirName);
BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset,
                      DVDCallback callback, s32 prio);
s32 DVDReadPrio(struct DVDFileInfo * fileInfo, void * addr, s32 length, s32 offset, s32 prio);
int DVDSeekAsyncPrio(struct DVDFileInfo * fileInfo, s32 offset, void (* callback)(s32, struct DVDFileInfo *), s32 prio);
s32 DVDSeekPrio(struct DVDFileInfo * fileInfo, s32 offset, s32 prio);
s32 DVDGetFileInfoStatus(struct DVDFileInfo * fileInfo);
int DVDOpenDir(char * dirName, DVDDir * dir);
int DVDReadDir(DVDDir * dir, DVDDirEntry* dirent);
int DVDCloseDir(DVDDir* dir);
void * DVDGetFSTLocation();
BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback);
s32 DVDPrepareStream(DVDFileInfo* fileInfo, u32 length, u32 offset);
s32 DVDGetTransferredSize(DVDFileInfo* fileinfo);

#define DVDReadAsync(fileInfo, addr, length, offset, callback) \
    DVDReadAsyncPrio((fileInfo), (addr), (length), (offset), (callback), 2)

#define DVD_RESULT_GOOD        0
#define DVD_RESULT_FATAL_ERROR -1
#define DVD_RESULT_IGNORED     -2
#define DVD_RESULT_CANCELED    -6

#define DVD_STATE_FATAL_ERROR   -1
#define DVD_STATE_END            0
#define DVD_STATE_BUSY           1
#define DVD_STATE_WAITING        2
#define DVD_STATE_COVER_CLOSED   3
#define DVD_STATE_NO_DISK        4
#define DVD_STATE_COVER_OPEN     5
#define DVD_STATE_WRONG_DISK     6
#define DVD_STATE_MOTOR_STOPPED  7
#define DVD_STATE_PAUSING        8
#define DVD_STATE_IGNORED        9
#define DVD_STATE_CANCELED       10
#define DVD_STATE_RETRY          11

#define DVD_MIN_TRANSFER_SIZE    32

// could be bitfields
#define DVD_INTTYPE_TC 1
#define DVD_INTTYPE_DE 2
// unk type 3
#define DVD_INTTYPE_CVR 4

// DVD Commands

#define DVD_COMMAND_NONE 0
#define DVD_COMMAND_READ 1
#define DVD_COMMAND_SEEK 2
#define DVD_COMMAND_CHANGE_DISK 3
#define DVD_COMMAND_BSREAD 4
#define DVD_COMMAND_READID 5
#define DVD_COMMAND_INITSTREAM 6
#define DVD_COMMAND_CANCELSTREAM 7
#define DVD_COMMAND_STOP_STREAM_AT_END 8
#define DVD_COMMAND_REQUEST_AUDIO_ERROR 9
#define DVD_COMMAND_REQUEST_PLAY_ADDR 10
#define DVD_COMMAND_REQUEST_START_ADDR 11
#define DVD_COMMAND_REQUEST_LENGTH 12
#define DVD_COMMAND_AUDIO_BUFFER_CONFIG 13
#define DVD_COMMAND_INQUIRY 14
#define DVD_COMMAND_BS_CHANGE_DISK 15

// unidentified externs
extern int DVDReadAbsAsyncForBS(struct DVDCommandBlock * block, void * addr, s32 length, s32 offset, void (* callback)(s32, struct DVDCommandBlock *));
extern int DVDReadDiskID(struct DVDCommandBlock * block, struct DVDDiskID * diskID, void (* callback)(s32, struct DVDCommandBlock *));
extern void DVDReset(void);

int DVDReadAbsAsyncPrio(struct DVDCommandBlock * block /* r29 */, void * addr /* r1+0xC */, s32 length /* r1+0x10 */, s32 offset /* r1+0x14 */, void (* callback)(s32, struct DVDCommandBlock *) /* r1+0x18 */, s32 prio /* r31 */);
int DVDSeekAbsAsyncPrio(struct DVDCommandBlock * block /* r31 */, s32 offset /* r28 */, void (* callback)(s32, struct DVDCommandBlock *) /* r1+0x10 */, s32 prio /* r1+0x14 */);
int DVDPrepareStreamAbsAsync(struct DVDCommandBlock * block /* r31 */, u32 length /* r1+0xC */, u32 offset /* r1+0x10 */, void (* callback)(s32, struct DVDCommandBlock *) /* r1+0x14 */);
void __DVDStoreErrorCode(u32 error);

#endif
