#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <dolphin/ax.h>
#include "platform/audio_output.h"

static uint8_t aram[64];
static int16_t mixed[1200];
static size_t mixed_frames;

const unsigned char* NativeARAMPointer(u32 address, u32 length)
{
    if (address > sizeof(aram) || length > sizeof(aram) - address) return NULL;
    return aram + address;
}

size_t NativeAudioOutputSubmit(const int16_t* samples, size_t frames)
{
    if (frames > 600) frames = 600;
    memcpy(mixed, samples, frames * 2 * sizeof(*samples));
    mixed_frames = frames;
    return frames;
}

bool NativeAudioOutputStart(uint32_t sample_rate)
{
    (void) sample_rate;
    return true;
}
void NativeAudioOutputStop(void) {}
size_t NativeAudioOutputQueuedFrames(void) { return mixed_frames; }

int main(void)
{
    AXVPB* voice;
    AXPBADDR address = { 0 };
    AXPBVE envelope = { 32767, 0 };
    AXPBMIX mix = { 0 };
    AXPBSRC source = { 1, 0, 0, { 0, 0, 0, 0 } };

    /* Predictor 0, scale 0, followed by sixteen positive ADPCM nibbles. */
    aram[0] = 0;
    for (int i = 0; i < 8; ++i) aram[1 + i] = 0x12;
    address.format = 0;
    address.endAddressLo = 9;
    address.currentAddressLo = 0;
    mix.vL = 32767;
    mix.vR = 32767;

    AXInit();
    voice = AXAcquireVoice(1, NULL, 0);
    assert(voice != NULL);
    AXSetVoiceAddr(voice, &address);
    AXSetVoiceAdpcm(voice, &(AXPBADPCM){ 0 });
    AXSetVoiceAdpcmLoop(voice, &(AXPBADPCMLOOP){ 0 });
    AXSetVoiceSrc(voice, &source);
    AXSetVoiceVe(voice, &envelope);
    AXSetVoiceMix(voice, &mix);
    AXSetVoiceState(voice, 1);
    NativeAudioTick();

    assert(mixed_frames == 533);
    assert(mixed[0] > 0 && mixed[1] > 0);
    assert(mixed[0] == mixed[1]);
    assert(voice->pb.state == 0);
    AXFreeVoice(voice);

    /* PCM16 remains big endian in ARAM. */
    aram[0] = 0x40;
    aram[1] = 0x00;
    aram[2] = 0xC0;
    aram[3] = 0x00;
    memset(mixed, 0, sizeof(mixed));
    mixed_frames = 0;
    AXInit();
    voice = AXAcquireVoice(1, NULL, 0);
    assert(voice != NULL);
    address.format = 2;
    address.endAddressLo = 4;
    address.currentAddressLo = 0;
    AXSetVoiceAddr(voice, &address);
    AXSetVoiceSrc(voice, &source);
    AXSetVoiceVe(voice, &envelope);
    AXSetVoiceMix(voice, &mix);
    AXSetVoiceState(voice, 1);
    NativeAudioTick();
    assert(mixed[0] > 0 && mixed[2] < 0);
    assert(voice->pb.state == 0);
    AXFreeVoice(voice);

    /* Unknown DSP formats must fail closed instead of reading arbitrary ARAM. */
    memset(mixed, 0, sizeof(mixed));
    mixed_frames = 0;
    AXInit();
    voice = AXAcquireVoice(1, NULL, 0);
    assert(voice != NULL);
    address.format = 3;
    address.currentAddressLo = 0;
    AXSetVoiceAddr(voice, &address);
    AXSetVoiceSrc(voice, &source);
    AXSetVoiceVe(voice, &envelope);
    AXSetVoiceMix(voice, &mix);
    AXSetVoiceState(voice, 1);
    NativeAudioTick();
    assert(voice->pb.state == 0);
    AXFreeVoice(voice);
    return 0;
}
