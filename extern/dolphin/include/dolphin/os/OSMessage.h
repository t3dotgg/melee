#ifndef _DOLPHIN_OSMESSAGE_H_
#define _DOLPHIN_OSMESSAGE_H_

#include <dolphin/types.h>

#include <dolphin/os/OSThread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct OSMessageQueue {
    struct OSThreadQueue queueSend;
    struct OSThreadQueue queueReceive;
    void * msgArray;
    s32 msgCount;
    s32 firstIndex;
    s32 usedCount;
};

void OSInitMessageQueue(struct OSMessageQueue * mq, void * msgArray, s32 msgCount);
int OSSendMessage(struct OSMessageQueue * mq, void * msg, s32 flags);
int OSReceiveMessage(struct OSMessageQueue * mq, void * msg, s32 flags);
int OSJamMessage(struct OSMessageQueue * mq, void * msg, s32 flags);

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_OSMESSAGE_H_
