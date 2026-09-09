/*
 * Host implementation of the Dolphin audio interfaces.
 *
 * The native port does not have the GameCube DSP or AI hardware.  Keep the
 * API stateful enough for the game synthesizer to run, but do not generate
 * audio or start a background thread.  This also keeps tests deterministic.
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

BOOL AIGetDMAEnableFlag(void) { return ai_dma_enabled ? TRUE : FALSE; }

void AIStartDMA(void)
{
    ai_dma_enabled = true;
#ifdef MELEE_NATIVE
    /* GameCube AI buffers contain interleaved signed 16-bit stereo samples.
     * The host queue consumes the same format. Native DMA completes
     * synchronously after the copy, which keeps callers deterministic. */
    if (ai_dma_start != 0 && ai_dma_length >= 4 &&
        (ai_dma_length & 3u) == 0) {
        NativeAudioOutputSubmit((const int16_t*) (uintptr_t) ai_dma_start,
                                 ai_dma_length / 4);
        ai_dma_enabled = false;
        if (ai_dma_callback != NULL) {
            ai_dma_callback();
        }
    }
#endif
}

void AIStopDMA(void) { ai_dma_enabled = false; }

u32 AIGetDMABytesLeft(void) { return ai_dma_enabled ? ai_dma_length : 0; }

uptr AIGetDMAStartAddr(void) { return ai_dma_start; }

u32 AIGetDMALength(void) { return ai_dma_length; }

BOOL AICheckInit(void) { return ai_initialized ? TRUE : FALSE; }

AISCallback AIRegisterStreamCallback(AISCallback callback)
{
    AISCallback old = ai_stream_callback;
    ai_stream_callback = callback;
    return old;
}

u32 AIGetStreamSampleCount(void) { return ai_stream_sample_count; }

void AIResetStreamSampleCount(void) { ai_stream_sample_count = 0; }

void AISetStreamTrigger(u32 trigger) { ai_stream_trigger = trigger; }

u32 AIGetStreamTrigger(void) { return ai_stream_trigger; }

void AISetStreamPlayState(u32 state) { ai_stream_play_state = state; }

u32 AIGetStreamPlayState(void) { return ai_stream_play_state; }

void AISetDSPSampleRate(u32 rate) { ai_dsp_sample_rate = rate; }

u32 AIGetDSPSampleRate(void) { return ai_dsp_sample_rate; }

void AISetStreamSampleRate(u32 rate) { ai_stream_sample_rate = rate; }

u32 AIGetStreamSampleRate(void) { return ai_stream_sample_rate; }

void AISetStreamVolLeft(u8 vol) { ai_stream_volume_left = vol; }

u8 AIGetStreamVolLeft(void) { return ai_stream_volume_left; }

void AISetStreamVolRight(u8 vol) { ai_stream_volume_right = vol; }

u8 AIGetStreamVolRight(void) { return ai_stream_volume_right; }

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

/* State that the GameCube DSP normally keeps while decoding each voice. */
static u32 native_ax_block_address[AX_MAX_VOICES];
static u8 native_ax_block_position[AX_MAX_VOICES];
static bool native_ax_block_valid[AX_MAX_VOICES];
static s16 native_ax_block_samples[AX_MAX_VOICES][16];
static float native_ax_source_position[AX_MAX_VOICES];
static u32 native_ax_remainder;

extern const unsigned char* NativeARAMPointer(u32 address, u32 length);

static inline u32 native_ax_address(u16 hi, u16 lo)
{
    return ((u32) hi << 16) | lo;
}

static inline s16 native_ax_clamp(s32 value)
{
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return (s16) value;
}

static void native_ax_decode_block(AXVPB* voice, u32 address)
{
    /* ADPCM addresses use nibbles. Each frame has a one byte header followed
     * by seven bytes containing fourteen samples. The PB current address
     * points at the first sample nibble, two nibbles after the frame start. */
    const u32 frame = address >= 2 ? (address - 2) & ~0xFu : 0;
    const u32 byte_address = frame / 2;
    const u8* data = NativeARAMPointer(byte_address, 8);
    int predictor;
    int scale;
    s32 yn1;
    s32 yn2;

    if (data == NULL) {
        memset(native_ax_block_samples[voice->index], 0,
               sizeof(native_ax_block_samples[voice->index]));
        return;
    }
    predictor = data[0] >> 4;
    scale = data[0] & 0xF;
    if (predictor >= 8) predictor = 0;
    yn1 = (s16) voice->pb.adpcm.yn1;
    yn2 = (s16) voice->pb.adpcm.yn2;
    for (int i = 0; i < 14; ++i) {
        int nibble = (i & 1) == 0 ? data[1 + i / 2] >> 4
                                  : data[1 + i / 2] & 0xF;
        if (nibble >= 8) nibble -= 16;
        /* Left-shifting a negative signed value is undefined in C. */
        s32 sample = (s32) nibble * (1 << scale);
        sample += ((s16) voice->pb.adpcm.a[predictor][0] * yn1 +
                   (s16) voice->pb.adpcm.a[predictor][1] * yn2) >> 11;
        sample = native_ax_clamp(sample);
        native_ax_block_samples[voice->index][i] = (s16) sample;
        yn2 = yn1;
        yn1 = sample;
    }
    voice->pb.adpcm.yn1 = (u16) yn1;
    voice->pb.adpcm.yn2 = (u16) yn2;
}

static bool native_ax_advance(AXVPB* voice)
{
    u32 current = native_ax_address(voice->pb.addr.currentAddressHi,
                                     voice->pb.addr.currentAddressLo);
    const u32 end = native_ax_address(voice->pb.addr.endAddressHi,
                                      voice->pb.addr.endAddressLo);
    const u32 loop = native_ax_address(voice->pb.addr.loopAddressHi,
                                       voice->pb.addr.loopAddressLo);
    /* PCM16 addresses are 16-bit words. PCM8 addresses are bytes. */
    const u32 stride = 1;

    if (voice->pb.addr.format == NATIVE_AX_FORMAT_ADPCM) {
        const u32 frame = current >= 2 ? (current - 2) & ~0xFu : 0;
        current = frame + 0x12; /* next frame's first sample nibble */
    } else {
        current += stride;
    }
    if (end != 0 && current > end) {
        if (voice->pb.addr.loopFlag != 0 && loop < end) {
            current = loop;
            voice->pb.adpcm.pred_scale = voice->pb.adpcmLoop.loop_pred_scale;
            voice->pb.adpcm.yn1 = voice->pb.adpcmLoop.loop_yn1;
            voice->pb.adpcm.yn2 = voice->pb.adpcmLoop.loop_yn2;
        } else {
            voice->pb.state = 0;
            return false;
        }
    }
    voice->pb.addr.currentAddressHi = (u16) (current >> 16);
    voice->pb.addr.currentAddressLo = (u16) current;
    native_ax_block_valid[voice->index] = false;
    return true;
}

static bool native_ax_sample(AXVPB* voice, s16* output)
{
    const u32 index = voice->index;
    const u32 address = native_ax_address(voice->pb.addr.currentAddressHi,
                                          voice->pb.addr.currentAddressLo);
    const u32 end = native_ax_address(voice->pb.addr.endAddressHi,
                                      voice->pb.addr.endAddressLo);
    if (voice->pb.state == 0 || (end != 0 && address > end)) {
        voice->pb.state = 0;
        return false;
    }
    if (voice->pb.addr.format != NATIVE_AX_FORMAT_ADPCM &&
        voice->pb.addr.format != NATIVE_AX_FORMAT_PCM8 &&
        voice->pb.addr.format != NATIVE_AX_FORMAT_PCM16) {
        /* AX formats outside these three GameCube encodings need DSP code
         * that the host mixer does not provide. Stop the voice cleanly. */
        voice->pb.state = 0;
        return false;
    }
    if (voice->pb.addr.format == NATIVE_AX_FORMAT_ADPCM) {
        const u32 frame = address >= 2 ? (address - 2) & ~0xFu : 0;
        if (!native_ax_block_valid[index] ||
            native_ax_block_address[index] != frame) {
            native_ax_decode_block(voice, address);
            native_ax_block_address[index] = frame;
            native_ax_block_position[index] = 0;
            native_ax_block_valid[index] = true;
        }
        const u32 frame_pos = address - frame - 2;
        *output = native_ax_block_samples[index][frame_pos];
        if (frame_pos == 13) {
            native_ax_block_position[index] = 0;
            native_ax_advance(voice);
        } else {
            native_ax_block_position[index] = (u8) (frame_pos + 1);
            const u32 next = address + 1;
            voice->pb.addr.currentAddressHi = (u16) (next >> 16);
            voice->pb.addr.currentAddressLo = (u16) next;
        }
        return true;
    }
    const u32 byte_address = voice->pb.addr.format == NATIVE_AX_FORMAT_PCM8
                                 ? address
                                 : address * 2;
    const u8* data = NativeARAMPointer(
        byte_address, voice->pb.addr.format == NATIVE_AX_FORMAT_PCM8 ? 1 : 2);
    if (data == NULL) {
        voice->pb.state = 0;
        return false;
    }
    if (voice->pb.addr.format == NATIVE_AX_FORMAT_PCM8) {
        *output = (s16) ((s8) data[0] << 8);
    } else {
        *output = (s16) (((u16) data[0] << 8) | data[1]);
    }
    native_ax_advance(voice);
    return true;
}

static void native_ax_render(u32 frames)
{
    int16_t samples[600 * 2];
    memset(samples, 0, sizeof(samples));
    if (frames > 600) frames = 600;
    for (u32 i = 0; i < AX_MAX_VOICES; ++i) {
        AXVPB* voice = &ax_voices[i];
        if (!ax_voice_used[i] || voice->pb.state == 0) continue;
        float ratio = (float) (((u32) voice->pb.src.ratioHi << 16) |
                               voice->pb.src.ratioLo) / 65536.0f;
        if (!(ratio > 0.0f)) ratio = 1.0f;
        /* The compact host mixer does not resample below the source rate. */
        if (ratio < 1.0f) ratio = 1.0f;
        const float gain_l = (float) voice->pb.ve.currentVolume / 32767.0f *
                             (float) voice->pb.mix.vL / 32767.0f;
        const float gain_r = (float) voice->pb.ve.currentVolume / 32767.0f *
                             (float) voice->pb.mix.vR / 32767.0f;
        for (u32 frame = 0; frame < frames && voice->pb.state != 0; ++frame) {
            s16 sample;
            if (!native_ax_sample(voice, &sample)) break;
            samples[frame * 2] = native_ax_clamp(
                samples[frame * 2] + (s32) (sample * gain_l));
            samples[frame * 2 + 1] = native_ax_clamp(
                samples[frame * 2 + 1] + (s32) (sample * gain_r));
            /* One source sample was consumed above. Skip additional source
             * samples for rates above unity while retaining a fractional
             * phase for ratios such as 1.5. */
            native_ax_source_position[i] += ratio - 1.0f;
            while (native_ax_source_position[i] >= 1.0f &&
                   voice->pb.state != 0) {
                s16 discard;
                native_ax_source_position[i] -= 1.0f;
                (void) native_ax_sample(voice, &discard);
            }
        }
    }
    (void) NativeAudioOutputSubmit(samples, frames);
}
#endif

void AXInit(void)
{
    memset(ax_voices, 0, sizeof(ax_voices));
    memset(ax_voice_used, 0, sizeof(ax_voice_used));
#ifdef MELEE_NATIVE
    memset(native_ax_block_address, 0, sizeof(native_ax_block_address));
    memset(native_ax_block_position, 0, sizeof(native_ax_block_position));
    memset(native_ax_block_valid, 0, sizeof(native_ax_block_valid));
    memset(native_ax_source_position, 0, sizeof(native_ax_source_position));
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
    for (u32 i = 0; i < AX_MAX_VOICES; i++) {
        if (!ax_voice_used[i]) {
            AXVPB* voice = &ax_voices[i];
            memset(voice, 0, sizeof(*voice));
            voice->priority = (int) priority;
            voice->callback = callback;
            voice->userContext = user_context;
            voice->index = i;
            ax_voice_used[i] = true;
            return voice;
        }
    }
    return NULL;
}

void AXFreeVoice(AXVPB* voice)
{
    if (voice == NULL || voice < ax_voices ||
        voice >= ax_voices + AX_MAX_VOICES) {
        return;
    }
    const size_t index = (size_t) (voice - ax_voices);
    ax_voice_used[index] = false;
#ifdef MELEE_NATIVE
    native_ax_block_valid[index] = false;
    native_ax_source_position[index] = 0.0f;
#endif
    memset(voice, 0, sizeof(*voice));
}

void AXSetVoicePriority(AXVPB* voice, u32 priority)
{
    if (voice != NULL) voice->priority = (int) priority;
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

void AXSetMode(u32 mode) { ax_mode = mode; }

u32 AXGetMode(void) { return ax_mode; }

void AXRegisterCallback(void (*callback)(void)) { ax_callback = callback; }

#ifdef MELEE_NATIVE
/* The GameCube invokes the AX callback from the DSP interrupt. Native builds
 * have no DSP interrupt, so drive the same callback once per video retrace. */
void NativeAudioTick(void)
{
#ifdef MELEE_NATIVE
    native_ax_remainder += 32000;
    u32 frames = native_ax_remainder / 60;
    native_ax_remainder %= 60;
    native_ax_render(frames);
#endif
    if (ax_callback != NULL) {
        ax_callback();
    }
}
#endif

void AXInitProfile(AXPROFILE* profile, u32 max_profiles)
{
    (void) max_profiles;
    if (profile != NULL) memset(profile, 0, sizeof(*profile));
}

u32 AXGetProfile(void) { return 0; }

void AXSetVoiceSrcType(AXVPB* voice, u32 type)
{
    if (voice != NULL) voice->pb.type = (u16) type;
}

void AXSetVoiceState(AXVPB* voice, u16 state)
{
    if (voice != NULL) voice->pb.state = state;
}

void AXSetVoiceType(AXVPB* voice, u16 type)
{
    if (voice != NULL) voice->pb.type = type;
}

void AXSetVoiceMix(AXVPB* voice, AXPBMIX* mix)
{
    if (voice != NULL && mix != NULL) memcpy(&voice->pb.mix, mix, sizeof(*mix));
}

void AXSetVoiceItdOn(AXVPB* voice)
{
    if (voice != NULL) voice->pb.itd.flag = 1;
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
    if (voice != NULL) voice->pb.update.updNum[0]++;
}

void AXSetVoiceUpdateWrite(AXVPB* voice, u16 param, u16 data)
{
    if (voice != NULL && param < 128) voice->updateData[param] = data;
}

void AXSetVoiceDpop(AXVPB* voice, AXPBDPOP* dpop)
{
    if (voice != NULL && dpop != NULL) memcpy(&voice->pb.dpop, dpop, sizeof(*dpop));
}

void AXSetVoiceVe(AXVPB* voice, AXPBVE* ve)
{
    if (voice != NULL && ve != NULL) memcpy(&voice->pb.ve, ve, sizeof(*ve));
}

void AXSetVoiceVeDelta(AXVPB* voice, s16 delta)
{
    if (voice != NULL) voice->pb.ve.currentDelta = delta;
}

void AXSetVoiceFir(AXVPB* voice, AXPBFIR* fir)
{
    if (voice != NULL && fir != NULL) memcpy(&voice->pb.fir, fir, sizeof(*fir));
}

static void AXSetAddress(u16* high, u16* low, u32 address)
{
    *high = (u16) (address >> 16);
    *low = (u16) address;
}

void AXSetVoiceAddr(AXVPB* voice, AXPBADDR* addr)
{
    if (voice != NULL && addr != NULL) memcpy(&voice->pb.addr, addr, sizeof(*addr));
}

void AXSetVoiceLoop(AXVPB* voice, u16 loop)
{
    if (voice != NULL) voice->pb.addr.loopFlag = loop;
}

void AXSetVoiceLoopAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) AXSetAddress(&voice->pb.addr.loopAddressHi, &voice->pb.addr.loopAddressLo, address);
}

void AXSetVoiceEndAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) AXSetAddress(&voice->pb.addr.endAddressHi, &voice->pb.addr.endAddressLo, address);
}

void AXSetVoiceCurrentAddr(AXVPB* voice, u32 address)
{
    if (voice != NULL) AXSetAddress(&voice->pb.addr.currentAddressHi, &voice->pb.addr.currentAddressLo, address);
}

void AXSetVoiceAdpcm(AXVPB* voice, AXPBADPCM* adpcm)
{
    if (voice != NULL && adpcm != NULL) memcpy(&voice->pb.adpcm, adpcm, sizeof(*adpcm));
}

void AXSetVoiceSrc(AXVPB* voice, AXPBSRC* src)
{
    if (voice != NULL && src != NULL) memcpy(&voice->pb.src, src, sizeof(*src));
}

void AXSetVoiceSrcRatio(AXVPB* voice, float ratio)
{
    u32 bits;
    if (voice == NULL) return;
    memcpy(&bits, &ratio, sizeof(bits));
    voice->pb.src.ratioHi = (u16) (bits >> 16);
    voice->pb.src.ratioLo = (u16) bits;
}

void AXSetVoiceAdpcmLoop(AXVPB* voice, AXPBADPCMLOOP* loop)
{
    if (voice != NULL && loop != NULL) memcpy(&voice->pb.adpcmLoop, loop, sizeof(*loop));
}

void AXSetMaxDspCycles(u32 cycles) { ax_max_dsp_cycles = cycles; }

u32 AXGetMaxDspCycles(void) { return ax_max_dsp_cycles; }

u32 AXGetDspCycles(void) { return 0; }

static void* (*axfx_alloc_hook)(u32);
static void (*axfx_free_hook)(void*);

void AXFXSetHooks(void* (*alloc_hook)(u32), void (*free_hook)(void*))
{
    axfx_alloc_hook = alloc_hook;
    axfx_free_hook = free_hook;
}

void* AXFXAllocFunction(u32 size)
{
    return axfx_alloc_hook != NULL ? axfx_alloc_hook(size) : malloc((size_t) size);
}

void AXFXFreeFunction(void* ptr)
{
    if (axfx_free_hook != NULL) axfx_free_hook(ptr);
    else free(ptr);
}

int AXFXChorusInit(struct AXFX_CHORUS* chorus)
{
    if (chorus == NULL) return 0;
    memset(&chorus->work, 0, sizeof(chorus->work));
    return 1;
}

int AXFXChorusShutdown(struct AXFX_CHORUS* chorus) { (void) chorus; return 1; }
int AXFXChorusSettings(struct AXFX_CHORUS* chorus) { return chorus != NULL; }
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* buffer, struct AXFX_CHORUS* chorus)
{
    (void) buffer;
    (void) chorus;
}

int AXFXDelayInit(struct AXFX_DELAY* delay)
{
    if (delay == NULL) return 0;
    memset(delay->currentSize, 0, sizeof(delay->currentSize));
    memset(delay->currentPos, 0, sizeof(delay->currentPos));
    return 1;
}

int AXFXDelayShutdown(struct AXFX_DELAY* delay) { (void) delay; return 1; }
int AXFXDelaySettings(struct AXFX_DELAY* delay) { return delay != NULL; }
void AXFXDelayCallback(struct AXFX_BUFFERUPDATE* buffer, struct AXFX_DELAY* delay)
{
    (void) buffer;
    (void) delay;
}

int AXFXReverbHiInit(struct AXFX_REVERBHI* reverb)
{
    if (reverb == NULL) return 0;
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    return 1;
}

int AXFXReverbHiShutdown(struct AXFX_REVERBHI* reverb) { (void) reverb; return 1; }
int AXFXReverbHiSettings(struct AXFX_REVERBHI* reverb) { return reverb != NULL; }
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* buffer, struct AXFX_REVERBHI* reverb)
{
    (void) buffer;
    (void) reverb;
}

int AXFXReverbStdInit(struct AXFX_REVERBSTD* reverb)
{
    if (reverb == NULL) return 0;
    memset(&reverb->rv, 0, sizeof(reverb->rv));
    return 1;
}

int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* reverb) { (void) reverb; return 1; }
int AXFXReverbStdSettings(struct AXFX_REVERBSTD* reverb) { return reverb != NULL; }
void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* buffer, struct AXFX_REVERBSTD* reverb)
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
