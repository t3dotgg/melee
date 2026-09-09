#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/audio_output.h"
#include <dolphin/ax.h>

static uint8_t aram[4096];
static int16_t mixed[32000 * 2];
static size_t mixed_frames;
static unsigned callbacks;
static AXVPB* dropped_voice;

const unsigned char* NativeARAMPointer(u32 address, u32 length)
{
    if (address > sizeof(aram) || length > sizeof(aram) - address) {
        return NULL;
    }
    return aram + address;
}

size_t NativeAudioOutputSubmit(const int16_t* samples, size_t frames)
{
    assert(frames == 160);
    assert(mixed_frames + frames <= 32000);
    memcpy(mixed + mixed_frames * 2, samples, frames * 2 * sizeof(*samples));
    mixed_frames += frames;
    return frames;
}

bool NativeAudioOutputStart(uint32_t sample_rate)
{
    return sample_rate == 32000;
}
void NativeAudioOutputStop(void) {}
size_t NativeAudioOutputQueuedFrames(void)
{
    return mixed_frames;
}

static void reset_audio(void)
{
    AXInit();
    memset(aram, 0, sizeof(aram));
    memset(mixed, 0, sizeof(mixed));
    mixed_frames = 0;
    callbacks = 0;
}

static AXVPB* start_voice(u16 format, u32 current, u32 end)
{
    AXVPB* voice = AXAcquireVoice(1, NULL, 0);
    AXPBADDR address = { 0 };
    AXPBMIX mix = { 0 };
    assert(voice != NULL);
    address.format = format;
    mix.vL = mix.vR = 0x8000;
    AXSetVoiceAddr(voice, &address);
    AXSetVoiceCurrentAddr(voice, current);
    AXSetVoiceEndAddr(voice, end);
    AXSetVoiceSrcType(voice, AX_SRC_TYPE_NONE);
    AXSetVoiceSrcRatio(voice, 1.0f);
    /* Half volume makes the expected PCM values exact integers. */
    AXSetVoiceVe(voice, &(AXPBVE){ 0x4000, 0 });
    AXSetVoiceMix(voice, &mix);
    AXSetVoiceState(voice, 1);
    return voice;
}

static void expect_stereo(size_t frame, int16_t sample)
{
    assert(frame < mixed_frames);
    assert(mixed[frame * 2] == sample);
    assert(mixed[frame * 2 + 1] == sample);
}

static void put_pcm(size_t index, int16_t sample)
{
    aram[index * 2] = (uint16_t) sample >> 8;
    aram[index * 2 + 1] = (uint8_t) sample;
}

static void test_adpcm(void)
{
    reset_audio();
    memset(aram, 0x12, 16);
    aram[0] = 1;
    aram[7] = 0xF1;
    aram[8] = 2;
    AXVPB* voice = start_voice(0, 2, 19);
    AXSetVoiceAdpcm(voice, &(AXPBADPCM){ .pred_scale = 1 });
    NativeAudioTick();
    for (size_t i = 0; i < 12; ++i) {
        expect_stereo(i, 1 + (i & 1));
    }
    expect_stereo(12, -1);
    expect_stereo(13, 1);
    expect_stereo(14, 2);
    expect_stereo(15, 4);
    expect_stereo(16, 0);
    assert(voice->pb.state == 0);
    assert(voice->pb.adpcm.yn1 == 8);
    assert(voice->pb.adpcm.yn2 == 4);

    /* Predictor rounding and saturation use the preceding decoded sample. */
    reset_audio();
    aram[1] = 0x10;
    voice = start_voice(0, 2, 3);
    AXPBADPCM adpcm = { 0 };
    adpcm.a[1][0] = 1024;
    adpcm.pred_scale = 0x11;
    adpcm.yn1 = 3;
    AXSetVoiceAdpcm(voice, &adpcm);
    NativeAudioTick();
    expect_stereo(0, 2); /* 2 + round(3 / 2) = 4 */
    expect_stereo(1, 1);
    assert(voice->pb.adpcm.yn1 == 2);

    reset_audio();
    aram[1] = 0x78;
    voice = start_voice(0, 2, 3);
    AXSetVoiceAdpcm(voice, &(AXPBADPCM){ .pred_scale = 15 });
    NativeAudioTick();
    expect_stereo(0, 16383);
    expect_stereo(1, -16384);
}

static void test_adpcm_loops(void)
{
    reset_audio();
    memset(aram, 0x11, sizeof(aram));
    AXVPB* voice = start_voice(0, 2, 5);
    AXPBADPCM adpcm = { 0 };
    adpcm.a[0][0] = 2048;
    adpcm.pred_scale = 1;
    AXSetVoiceAdpcm(voice, &adpcm);
    AXSetVoiceLoop(voice, 1);
    AXSetVoiceLoopAddr(voice, 3);
    AXSetVoiceAdpcmLoop(voice, &(AXPBADPCMLOOP){ 1, 100, 0 });
    NativeAudioTick();
    for (size_t i = 0; i < 4; ++i) {
        expect_stereo(i, (int16_t) (i + 1));
    }
    for (size_t i = 4; i < mixed_frames; ++i) {
        expect_stereo(i, (int16_t) (51 + (i - 4) % 3));
    }
    assert(voice->pb.state == 1);

    /* Music blocks can loop forward before the synth updates their end. */
    reset_audio();
    memset(aram, 0x11, sizeof(aram));
    voice = start_voice(0, 2, 3);
    AXSetVoiceAdpcm(voice, &adpcm);
    AXSetVoiceLoop(voice, 1);
    AXSetVoiceLoopAddr(voice, 18);
    AXSetVoiceAdpcmLoop(voice, &(AXPBADPCMLOOP){ 1, 100, 0 });
    NativeAudioTick();
    expect_stereo(0, 1);
    expect_stereo(1, 2);
    expect_stereo(2, 51);
    expect_stereo(3, 52);
    assert(voice->pb.state == 1);
    assert(voice->pb.addr.currentAddressLo > 18);
}

static void test_pcm_and_bounds(void)
{
    reset_audio();
    put_pcm(0, 16384);
    AXVPB* voice = start_voice(0x0A, 0, 0);
    NativeAudioTick();
    expect_stereo(0, 8192);
    expect_stereo(1, 0);
    assert(voice->pb.state == 0);

    reset_audio();
    aram[0] = 0x80;
    aram[1] = 0x7F;
    voice = start_voice(0x19, 0, 1);
    NativeAudioTick();
    expect_stereo(0, -16384);
    expect_stereo(1, 16256);
    expect_stereo(2, 0);
    assert(voice->pb.state == 0);

    const u16 formats[] = { 0, 0x19, 0x0A, 3 };
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        reset_audio();
        voice = start_voice(formats[i], UINT32_MAX - 1, UINT32_MAX);
        NativeAudioTick();
        assert(voice->pb.state == 0);
        for (size_t j = 0; j < mixed_frames; ++j) {
            expect_stereo(j, 0);
        }
    }
}

static void test_resampling(void)
{
    const float ratios[] = { 0, 0.5f, 1, 1.5f, 2, 4 };
    for (size_t i = 0; i < sizeof(ratios) / sizeof(ratios[0]); ++i) {
        reset_audio();
        for (size_t j = 0; j < 2048; ++j) {
            put_pcm(j, (int16_t) ((j + 1) * 16));
        }
        AXVPB* voice = start_voice(0x0A, 0, 2047);
        AXSetVoiceSrcType(voice, AX_SRC_TYPE_LINEAR);
        AXSetVoiceSrcRatio(voice, ratios[i]);
        NativeAudioTick();
        assert(mixed_frames == 480);
        assert(voice->pb.addr.currentAddressLo == (u32) (480 * ratios[i]));
        assert(voice->pb.state == 1);
        if (ratios[i] == 0) {
            for (size_t j = 0; j < mixed_frames; ++j) {
                expect_stereo(j, 0);
            }
        } else {
            /* AX's history delays the ramp by three source samples. */
            for (size_t j = 8; j < mixed_frames; ++j) {
                int16_t expected = (int16_t) (((j + 1) * ratios[i] - 3) * 8);
                expect_stereo(j, expected);
            }
        }
        if (ratios[i] == 0.5f) {
            NativeAudioTick();
            assert(voice->pb.addr.currentAddressLo == 480);
            expect_stereo(480, 1900);
        }
        AXSetVoiceSrcRatio(voice, 100);
        assert(voice->pb.src.ratioHi == 4 && voice->pb.src.ratioLo == 0);
        AXSetVoiceSrcRatio(voice, NAN);
        assert(voice->pb.src.ratioHi == 0 && voice->pb.src.ratioLo == 0);
        AXSetVoiceSrcType(voice, AX_SRC_TYPE_4TAP_16K);
        assert(voice->pb.srcSelect == 0 && voice->pb.coefSelect == 2);
        assert(voice->pb.type == 0);
    }

    /* A ratio change preserves fractional position and the sample history. */
    reset_audio();
    for (size_t i = 0; i < 2048; ++i) {
        put_pcm(i, (int16_t) (i * 16));
    }
    AXVPB* voice = start_voice(0x0A, 0, 2047);
    AXPBSRC source = { 0, 32769, 123, { 2, 4, 6, 8 } };
    AXSetVoiceSrcType(voice, AX_SRC_TYPE_LINEAR);
    AXSetVoiceSrc(voice, &source);
    NativeAudioTick();
    assert(voice->pb.addr.currentAddressLo == 240);
    assert(voice->pb.src.currentAddressFrac == 603);
    AXSetVoiceSrcRatio(voice, 1.5f);
    NativeAudioTick();
    assert(voice->pb.addr.currentAddressLo == 960);
    assert(voice->pb.src.currentAddressFrac == 603);
}

static void count_callback(void)
{
    callbacks++;
    assert(mixed_frames == callbacks * 160);
}

static void test_clock_and_fades(void)
{
    reset_audio();
    AXRegisterCallback(count_callback);
    for (int i = 0; i < 60; ++i) {
        NativeAudioTick();
    }
    assert(callbacks == 200);
    assert(mixed_frames == 32000);

    reset_audio();
    put_pcm(0, 16384);
    AXVPB* voice = start_voice(0x0A, 0, 0);
    AXSetVoiceLoop(voice, 1);
    AXSetVoiceLoopAddr(voice, 0);
    AXSetVoiceVeDelta(voice, -1);
    NativeAudioTick();
    expect_stereo(0, 8192);
    expect_stereo(2, 8191);
    expect_stereo(478, 7953);
    assert(voice->pb.ve.currentVolume == 16384 - 480);
}

static void test_mix_saturation(void)
{
    reset_audio();
    put_pcm(0, 30000);
    put_pcm(1, -30000);
    for (int i = 0; i < 3; ++i) {
        AXVPB* voice = start_voice(0x0A, i == 2 ? 1 : 0, i == 2 ? 1 : 0);
        AXSetVoiceVe(voice, &(AXPBVE){ 32767, 0 });
    }
    NativeAudioTick();
    expect_stereo(0, 29998);
}

static void record_drop(void* voice)
{
    dropped_voice = voice;
    assert(dropped_voice->userContext == 42);
}

static void test_voice_pool(void)
{
    reset_audio();
    AXVPB* first = AXAcquireVoice(1, record_drop, 42);
    for (int i = 1; i < AX_MAX_VOICES; ++i) {
        AXVPB* voice = AXAcquireVoice(1, record_drop, 42);
        assert(voice != NULL && voice->index == (u32) i);
    }
    assert(AXAcquireVoice(1, NULL, 0) == NULL);
    dropped_voice = NULL;
    assert(AXAcquireVoice(2, NULL, 0) == first);
    assert(dropped_voice == first);
    AXVPB* second = AXAcquireVoice(2, NULL, 0);
    assert(second != NULL && second->index == 1);
    AXFreeVoice(second);
    assert(second->index == 1);
    assert(second->pb.state == 0);
    assert(AXAcquireVoice(1, NULL, 0) == second);
}

int main(void)
{
    test_adpcm();
    test_adpcm_loops();
    test_pcm_and_bounds();
    test_resampling();
    test_clock_and_fades();
    test_mix_saturation();
    test_voice_pool();
    return 0;
}
