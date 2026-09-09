/*
 * Host implementation of the Dolphin audio interfaces.
 *
 * The native AX mixer decodes ARAM voices on the game thread. Core Audio
 * consumes the resulting stereo PCM through a separate output queue.
 */

#include <dolphin/ai.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>

#ifdef MELEE_NATIVE
#include "audio_output.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* AX exports these objects.  They are data-only on the host. */
AXPROFILE __AXLocalProfile;
DSPTaskInfo task;
u16 ax_dram_image[8192];
u16 axDspSlaveLength;
u16 axDspSlave[AX_DSP_SLAVE_LENGTH];

static bool ai_initialized;
static AIDCallback ai_dma_callback;
static AISCallback ai_stream_callback;
static uptr ai_dma_start;
static u32 ai_dma_length;
static bool ai_dma_enabled;
static u32 ai_stream_sample_count;
static u32 ai_stream_trigger;
static u32 ai_stream_play_state;
static u32 ai_dsp_sample_rate;
static u32 ai_stream_sample_rate;
static u8 ai_stream_volume_left;
static u8 ai_stream_volume_right;

static AXVPB ax_voices[AX_MAX_VOICES];
static bool ax_voice_used[AX_MAX_VOICES];
static u64 ax_voice_order[AX_MAX_VOICES];
static u64 ax_next_voice_order;

AIDCallback AIRegisterDMACallback(AIDCallback callback)
{
    AIDCallback old = ai_dma_callback;
    ai_dma_callback = callback;
    return old;
}

void AIInitDMA(uptr start_addr, u32 length)
{
    ai_dma_start = start_addr;
    ai_dma_length = length;
    ai_dma_enabled = false;
}

BOOL AIGetDMAEnableFlag(void)
{
    return ai_dma_enabled ? TRUE : FALSE;
}

void AIStartDMA(void)
{
    ai_dma_enabled = true;
#ifdef MELEE_NATIVE
    /* GameCube AI buffers contain interleaved signed 16-bit stereo samples.
     * The host queue consumes the same format. Native DMA completes
     * synchronously after the copy, which keeps callers deterministic. */
    if (ai_dma_start != 0 && ai_dma_length >= 4 && (ai_dma_length & 3u) == 0) {
        NativeAudioOutputSubmit((const int16_t*) (uintptr_t) ai_dma_start,
                                ai_dma_length / 4);
        ai_dma_enabled = false;
        if (ai_dma_callback != NULL) {
            ai_dma_callback();
        }
    }
#endif
}

void AIStopDMA(void)
{
    ai_dma_enabled = false;
}

u32 AIGetDMABytesLeft(void)
{
    return ai_dma_enabled ? ai_dma_length : 0;
}

uptr AIGetDMAStartAddr(void)
{
    return ai_dma_start;
}

u32 AIGetDMALength(void)
{
    return ai_dma_length;
}

BOOL AICheckInit(void)
{
    return ai_initialized ? TRUE : FALSE;
}

AISCallback AIRegisterStreamCallback(AISCallback callback)
{
    AISCallback old = ai_stream_callback;
    ai_stream_callback = callback;
    return old;
}

u32 AIGetStreamSampleCount(void)
{
    return ai_stream_sample_count;
}

void AIResetStreamSampleCount(void)
{
    ai_stream_sample_count = 0;
}

void AISetStreamTrigger(u32 trigger)
{
    ai_stream_trigger = trigger;
}

u32 AIGetStreamTrigger(void)
{
    return ai_stream_trigger;
}

void AISetStreamPlayState(u32 state)
{
    ai_stream_play_state = state;
}

u32 AIGetStreamPlayState(void)
{
    return ai_stream_play_state;
}

void AISetDSPSampleRate(u32 rate)
{
    ai_dsp_sample_rate = rate;
}

u32 AIGetDSPSampleRate(void)
{
    return ai_dsp_sample_rate;
}

void AISetStreamSampleRate(u32 rate)
{
    ai_stream_sample_rate = rate;
}

u32 AIGetStreamSampleRate(void)
{
    return ai_stream_sample_rate;
}

void AISetStreamVolLeft(u8 vol)
{
    ai_stream_volume_left = vol;
}

u8 AIGetStreamVolLeft(void)
{
    return ai_stream_volume_left;
}

void AISetStreamVolRight(u8 vol)
{
    ai_stream_volume_right = vol;
}

u8 AIGetStreamVolRight(void)
{
    return ai_stream_volume_right;
}

void AIInit(u8* stack)
{
    (void) stack;
    ai_initialized = true;
    ai_dma_enabled = false;
    ai_stream_sample_count = 0;
    ai_stream_play_state = AI_STREAM_STOP;
#ifdef MELEE_NATIVE
    /* Start the output device when available. Headless machines simply
     * continue with the state-only implementation. */
    (void) NativeAudioOutputStart(32000);
#endif
}

void AIReset(void)
{
    ai_initialized = false;
    ai_dma_enabled = false;
    ai_dma_start = 0;
    ai_dma_length = 0;
    ai_stream_sample_count = 0;
#ifdef MELEE_NATIVE
    NativeAudioOutputStop();
#endif
}

static u32 ax_mode;
static u32 ax_max_dsp_cycles;
static void (*ax_callback)(void);
static void (*ax_aux_a_callback)(void*, void*);
static void (*ax_aux_b_callback)(void*, void*);
static void* ax_aux_a_context;
static void* ax_aux_b_context;

#ifdef MELEE_NATIVE
#define NATIVE_AX_FORMAT_ADPCM 0
#define NATIVE_AX_FORMAT_PCM8 0x19
#define NATIVE_AX_FORMAT_PCM16 0x0A

/* AX renders 160 samples per DSP callback at 32 kHz. */
enum {
    NATIVE_AX_BLOCK_FRAMES = 160
};
static u32 native_ax_remainder;

extern const unsigned char* NativeARAMPointer(u32 address, u32 length);

static inline u32 native_ax_address(u16 hi, u16 lo)
{
    return ((u32) hi << 16) | lo;
}

static inline s16 native_ax_clamp(s64 value)
{
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return (s16) value;
}

static void native_ax_advance(AXVPB* voice, u32 current)
{
    const u32 end = native_ax_address(voice->pb.addr.endAddressHi,
                                      voice->pb.addr.endAddressLo);
    if (current == end) {
        current = native_ax_address(voice->pb.addr.loopAddressHi,
                                    voice->pb.addr.loopAddressLo);
        if (voice->pb.addr.loopFlag != 0) {
            voice->pb.adpcm.pred_scale = voice->pb.adpcmLoop.loop_pred_scale;
            if (voice->pb.type != 1) {
                voice->pb.adpcm.yn1 = voice->pb.adpcmLoop.loop_yn1;
                voice->pb.adpcm.yn2 = voice->pb.adpcmLoop.loop_yn2;
            }
        } else {
            voice->pb.state = 0;
        }
    } else {
        current++;
        if (voice->pb.addr.format == NATIVE_AX_FORMAT_ADPCM &&
            (current & 15) == 0)
        {
            const u8* header = NativeARAMPointer(current / 2, 1);
            if (header == NULL) {
                voice->pb.state = 0;
            } else {
                voice->pb.adpcm.pred_scale = *header;
            }
            current += 2;
        }
    }
    /* HPS loops can jump forward. The synth sets the new end address in its
     * next callback, so an address above the old end is not an error. */
    voice->pb.addr.currentAddressHi = (u16) (current >> 16);
    voice->pb.addr.currentAddressLo = (u16) current;
}

static s16 native_ax_sample(AXVPB* voice)
{
    const u32 address = native_ax_address(voice->pb.addr.currentAddressHi,
                                          voice->pb.addr.currentAddressLo);
    const u8* data;
    s16 output;
    if (voice->pb.state == 0) {
        return 0;
    }

    switch (voice->pb.addr.format) {
    case NATIVE_AX_FORMAT_ADPCM: {
        if ((address & 15) < 2) {
            voice->pb.state = 0;
            return 0;
        }
        data = NativeARAMPointer(address / 2, 1);
        if (data == NULL) {
            voice->pb.state = 0;
            return 0;
        }
        int nibble = (address & 1) ? *data & 15 : *data >> 4;
        if (nibble >= 8) {
            nibble -= 16;
        }
        const int predictor = (voice->pb.adpcm.pred_scale >> 4) & 7;
        const int scale = 1 << (voice->pb.adpcm.pred_scale & 15);
        const s64 prediction = (s64) (s16) voice->pb.adpcm.a[predictor][0] *
                                   (s16) voice->pb.adpcm.yn1 +
                               (s64) (s16) voice->pb.adpcm.a[predictor][1] *
                                   (s16) voice->pb.adpcm.yn2;
        output = native_ax_clamp(nibble * scale + ((prediction + 1024) >> 11));
        voice->pb.adpcm.yn2 = voice->pb.adpcm.yn1;
        voice->pb.adpcm.yn1 = (u16) output;
        break;
    }
    case NATIVE_AX_FORMAT_PCM8:
        data = NativeARAMPointer(address, 1);
        if (data == NULL) {
            voice->pb.state = 0;
            return 0;
        }
        output = (s16) ((s8) *data * 256);
        break;
    case NATIVE_AX_FORMAT_PCM16:
        if (address > UINT32_MAX / 2) {
            voice->pb.state = 0;
            return 0;
        }
        data = NativeARAMPointer(address * 2, 2);
        if (data == NULL) {
            voice->pb.state = 0;
            return 0;
        }
        output = (s16) (((u16) data[0] << 8) | data[1]);
        break;
    default:
        voice->pb.state = 0;
        return 0;
    }
    native_ax_advance(voice, address);
    return output;
}

static s16 native_ax_resample(AXVPB* voice)
{
    AXPBSRC* src = &voice->pb.src;
    if (voice->pb.srcSelect == 2) {
        return native_ax_sample(voice);
    }

    u32 ratio = native_ax_address(src->ratioHi, src->ratioLo);
    if (ratio > 0x40000) {
        ratio = 0x40000;
    }
    u32 phase = src->currentAddressFrac + ratio;
    while (phase >= 0x10000) {
        src->last_samples[0] = src->last_samples[1];
        src->last_samples[1] = src->last_samples[2];
        src->last_samples[2] = src->last_samples[3];
        src->last_samples[3] = (u16) native_ax_sample(voice);
        phase -= 0x10000;
    }
    src->currentAddressFrac = (u16) phase;
    /* Keep AX's four-sample history and phase across blocks. The native
     * mixer uses linear interpolation for the DSP ROM filter modes too. */
    const s32 left = (s16) src->last_samples[0];
    const s32 right = (s16) src->last_samples[1];
    return (s16) (((s64) left * (0x10000 - phase) + (s64) right * phase) >>
                  16);
}

static void native_ax_render(void)
{
    s32 accumulation[NATIVE_AX_BLOCK_FRAMES * 2] = { 0 };
    s16 samples[NATIVE_AX_BLOCK_FRAMES * 2];
    for (u32 i = 0; i < AX_MAX_VOICES; ++i) {
        AXVPB* voice = &ax_voices[i];
        if (!ax_voice_used[i] || voice->pb.state == 0) {
            continue;
        }
        for (u32 frame = 0; frame < NATIVE_AX_BLOCK_FRAMES; ++frame) {
            const s16 sample = native_ax_resample(voice);
            const s16 enveloped = native_ax_clamp(
                ((s32) sample * (s16) voice->pb.ve.currentVolume) >> 15);
            accumulation[frame * 2] +=
                native_ax_clamp(((s32) enveloped * voice->pb.mix.vL) >> 15);
            accumulation[frame * 2 + 1] +=
                native_ax_clamp(((s32) enveloped * voice->pb.mix.vR) >> 15);
            voice->pb.ve.currentVolume += voice->pb.ve.currentDelta;
            voice->pb.mix.vL += voice->pb.mix.vDeltaL;
            voice->pb.mix.vR += voice->pb.mix.vDeltaR;
        }
    }
    /* Sum voices before saturation so cancellation does not depend on
     * which voice received the first free slot. */
    for (u32 i = 0; i < NATIVE_AX_BLOCK_FRAMES * 2; ++i) {
        samples[i] = native_ax_clamp(accumulation[i]);
    }
    (void) NativeAudioOutputSubmit(samples, NATIVE_AX_BLOCK_FRAMES);
}

#endif

void AXInit(void)
{
    memset(ax_voices, 0, sizeof(ax_voices));
    memset(ax_voice_used, 0, sizeof(ax_voice_used));
    memset(ax_voice_order, 0, sizeof(ax_voice_order));
    ax_next_voice_order = 0;
    for (u32 i = 0; i < AX_MAX_VOICES; ++i) {
        ax_voices[i].index = i;
    }
#ifdef MELEE_NATIVE
    native_ax_remainder = 0;
#endif
    ax_mode = 0;
    ax_callback = NULL;
}

void AXQuit(void)
{
    memset(ax_voice_used, 0, sizeof(ax_voice_used));
}

AXVPB* AXAcquireVoice(u32 priority, void (*callback)(void*), uptr user_context)
{
    AXVPB* selected = NULL;
    for (u32 i = 0; i < AX_MAX_VOICES; i++) {
        AXVPB* voice = &ax_voices[i];
        if (!ax_voice_used[i]) {
            selected = voice;
            break;
        }
        if (voice->priority < (int) priority &&
            (selected == NULL || voice->priority < selected->priority ||
             (voice->priority == selected->priority &&
              ax_voice_order[i] < ax_voice_order[selected->index])))
        {
            selected = voice;
        }
    }
    if (selected == NULL) {
        return NULL;
    }
    const u32 index = selected->index;
    if (ax_voice_used[index] && selected->callback != NULL) {
        selected->callback(selected);
    }
    memset(selected, 0, sizeof(*selected));
    selected->priority = (int) priority;
    selected->callback = callback;
    selected->userContext = user_context;
    selected->index = index;
    ax_voice_used[index] = true;
    ax_voice_order[index] = ++ax_next_voice_order;
    return selected;
}

void AXFreeVoice(AXVPB* voice)
{
    if (voice == NULL || voice < ax_voices ||
        voice >= ax_voices + AX_MAX_VOICES)
    {
        return;
    }
    const size_t index = (size_t) (voice - ax_voices);
    ax_voice_used[index] = false;
    memset(voice, 0, sizeof(*voice));
    voice->index = (u32) index;
}

void AXSetVoicePriority(AXVPB* voice, u32 priority)
{
    if (voice != NULL) {
        voice->priority = (int) priority;
        ax_voice_order[voice->index] = ++ax_next_voice_order;
    }
}

void AXRegisterAuxACallback(void (*callback)(void*, void*), void* context)
{
    ax_aux_a_callback = callback;
    ax_aux_a_context = context;
}

void AXRegisterAuxBCallback(void (*callback)(void*, void*), void* context)
{
    ax_aux_b_callback = callback;
    ax_aux_b_context = context;
}

void AXSetMode(u32 mode)
{
    ax_mode = mode;
}

u32 AXGetMode(void)
{
    return ax_mode;
}

void AXRegisterCallback(void (*callback)(void))
{
    ax_callback = callback;
}

#ifdef MELEE_NATIVE
/* Keep DSP callbacks on their 5 ms clock while video advances at 60 Hz. */
void NativeAudioTick(void)
{
    native_ax_remainder += 32000;
    while (native_ax_remainder >= NATIVE_AX_BLOCK_FRAMES * 60) {
        native_ax_remainder -= NATIVE_AX_BLOCK_FRAMES * 60;
        native_ax_render();
        if (ax_callback != NULL) {
            ax_callback();
        }
    }
}
#endif

void AXInitProfile(AXPROFILE* profile, u32 max_profiles)
{
    (void) max_profiles;
    if (profile != NULL) {
        memset(profile, 0, sizeof(*profile));
    }
}

u32 AXGetProfile(void)
{
    return 0;
}

void AXSetVoiceSrcType(AXVPB* voice, u32 type)
{
    if (voice == NULL || type > AX_SRC_TYPE_4TAP_16K) {
        return;
    }
    voice->pb.srcSelect = type == AX_SRC_TYPE_NONE     ? 2
                          : type == AX_SRC_TYPE_LINEAR ? 1
                                                       : 0;
    if (type >= AX_SRC_TYPE_4TAP_8K) {
        voice->pb.coefSelect = (u16) (type - AX_SRC_TYPE_4TAP_8K);
    }
}

void AXSetVoiceState(AXVPB* voice, u16 state)
{
    if (voice != NULL) {
        voice->pb.state = state;
    }
}

void AXSetVoiceType(AXVPB* voice, u16 type)
{
    if (voice != NULL) {
        voice->pb.type = type;
    }
}

void AXSetVoiceMix(AXVPB* voice, AXPBMIX* mix)
{
    if (voice != NULL && mix != NULL) {
        memcpy(&voice->pb.mix, mix, sizeof(*mix));
    }
}

void AXSetVoiceItdOn(AXVPB* voice)
{
    if (voice != NULL) {
        voice->pb.itd.flag = 1;
    }
}

void AXSetVoiceItdTarget(AXVPB* voice, u16 left, u16 right)
{
    if (voice != NULL) {
        voice->pb.itd.targetShiftL = left;
        voice->pb.itd.targetShiftR = right;
    }
}

void AXSetVoiceUpdateIncrement(AXVPB* voice)
{
    if (voice != NULL) {
        voice->pb.update.updNum[0]++;
    }
}

void AXSetVoiceUpdateWrite(AXVPB* voice, u16 param, u16 data)
{
    if (voice != NULL && param < 128) {
        voice->updateData[param] = data;
    }
}

void AXSetVoiceDpop(AXVPB* voice, AXPBDPOP* dpop)
{
    if (voice != NULL && dpop != NULL) {
        memcpy(&voice->pb.dpop, dpop, sizeof(*dpop));
    }
}

void AXSetVoiceVe(AXVPB* voice, AXPBVE* ve)
{
    if (voice != NULL && ve != NULL) {
        memcpy(&voice->pb.ve, ve, sizeof(*ve));
    }
}

void AXSetVoiceVeDelta(AXVPB* voice, s16 delta)
{
    if (voice != NULL) {
        voice->pb.ve.currentDelta = delta;
    }
}

void AXSetVoiceFir(AXVPB* voice, AXPBFIR* fir)
{
    if (voice != NULL && fir != NULL) {
        memcpy(&voice->pb.fir, fir, sizeof(*fir));
    }
}

static void AXSetAddress(u16* high, u16* low, u32 address)
{
    *high = (u16) (address >> 16);
    *low = (u16) address;
}

void AXSetVoiceAddr(AXVPB* voice, AXPBADDR* addr)
{
    if (voice != NULL && addr != NULL) {
        memcpy(&voice->pb.addr, addr, sizeof(*addr));
    }
}

void AXSetVoiceLoop(AXVPB* voice, u16 loop)
{
    if (voice != NULL) {
        voice->pb.addr.loopFlag = loop;
    }
}

void AXSetVoiceLoopAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) {
        AXSetAddress(&voice->pb.addr.loopAddressHi,
                     &voice->pb.addr.loopAddressLo, address);
    }
}

void AXSetVoiceEndAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) {
        AXSetAddress(&voice->pb.addr.endAddressHi,
                     &voice->pb.addr.endAddressLo, address);
    }
}

void AXSetVoiceCurrentAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) {
        AXSetAddress(&voice->pb.addr.currentAddressHi,
                     &voice->pb.addr.currentAddressLo, address);
    }
}

void AXSetVoiceAdpcm(AXVPB* voice, AXPBADPCM* adpcm)
{
    if (voice != NULL && adpcm != NULL) {
        memcpy(&voice->pb.adpcm, adpcm, sizeof(*adpcm));
    }
}

void AXSetVoiceSrc(AXVPB* voice, AXPBSRC* src)
{
    if (voice != NULL && src != NULL) {
        memcpy(&voice->pb.src, src, sizeof(*src));
    }
}

void AXSetVoiceSrcRatio(AXVPB* voice, float ratio)
{
    u32 bits;
    if (voice == NULL) {
        return;
    }
    if (!(ratio > 0.0f)) {
        ratio = 0.0f;
    }
    if (ratio > 4.0f) {
        ratio = 4.0f;
    }
    bits = (u32) (ratio * 65536.0f);
    voice->pb.src.ratioHi = (u16) (bits >> 16);
    voice->pb.src.ratioLo = (u16) bits;
}

void AXSetVoiceAdpcmLoop(AXVPB* voice, AXPBADPCMLOOP* loop)
{
    if (voice != NULL && loop != NULL) {
        memcpy(&voice->pb.adpcmLoop, loop, sizeof(*loop));
    }
}

void AXSetMaxDspCycles(u32 cycles)
{
    ax_max_dsp_cycles = cycles;
}

u32 AXGetMaxDspCycles(void)
{
    return ax_max_dsp_cycles;
}

u32 AXGetDspCycles(void)
{
    return 0;
}

static void* (*axfx_alloc_hook)(u32);
static void (*axfx_free_hook)(void*);

void AXFXSetHooks(void* (*alloc_hook)(u32), void (*free_hook)(void*))
{
    axfx_alloc_hook = alloc_hook;
    axfx_free_hook = free_hook;
}

void* AXFXAllocFunction(u32 size)
{
    return axfx_alloc_hook != NULL ? axfx_alloc_hook(size)
                                   : malloc((size_t) size);
}

void AXFXFreeFunction(void* ptr)
{
    if (axfx_free_hook != NULL) {
        axfx_free_hook(ptr);
    } else {
        free(ptr);
    }
}

int AXFXChorusInit(struct AXFX_CHORUS* chorus)
{
    if (chorus == NULL) {
        return 0;
    }
    memset(&chorus->work, 0, sizeof(chorus->work));
    return 1;
}

int AXFXChorusShutdown(struct AXFX_CHORUS* chorus)
{
    (void) chorus;
    return 1;
}
int AXFXChorusSettings(struct AXFX_CHORUS* chorus)
{
    return chorus != NULL;
}
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* buffer,
                        struct AXFX_CHORUS* chorus)
{
    (void) buffer;
    (void) chorus;
}

int AXFXDelayInit(struct AXFX_DELAY* delay)
{
    if (delay == NULL) {
        return 0;
    }
    memset(delay->currentSize, 0, sizeof(delay->currentSize));
    memset(delay->currentPos, 0, sizeof(delay->currentPos));
    return 1;
}

int AXFXDelayShutdown(struct AXFX_DELAY* delay)
{
    (void) delay;
    return 1;
}
int AXFXDelaySettings(struct AXFX_DELAY* delay)
{
    return delay != NULL;
}
void AXFXDelayCallback(struct AXFX_BUFFERUPDATE* buffer,
                       struct AXFX_DELAY* delay)
{
    (void) buffer;
    (void) delay;
}

int AXFXReverbHiInit(struct AXFX_REVERBHI* reverb)
{
    if (reverb == NULL) {
        return 0;
    }
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    return 1;
}

int AXFXReverbHiShutdown(struct AXFX_REVERBHI* reverb)
{
    (void) reverb;
    return 1;
}
int AXFXReverbHiSettings(struct AXFX_REVERBHI* reverb)
{
    return reverb != NULL;
}
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* buffer,
                          struct AXFX_REVERBHI* reverb)
{
    (void) buffer;
    (void) reverb;
}

int AXFXReverbStdInit(struct AXFX_REVERBSTD* reverb)
{
    if (reverb == NULL) {
        return 0;
    }
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    return 1;
}

int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* reverb)
{
    (void) reverb;
    return 1;
}
int AXFXReverbStdSettings(struct AXFX_REVERBSTD* reverb)
{
    return reverb != NULL;
}
void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* buffer,
                           struct AXFX_REVERBSTD* reverb)
{
    (void) buffer;
    (void) reverb;
}

void DoCrossTalk(s32* left, s32* right, float cross, float invcross)
{
    (void) left;
    (void) right;
    (void) cross;
    (void) invcross;
}
