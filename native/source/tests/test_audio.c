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

    /* Predictor 0, scale 0, followed by fourteen ADPCM nibbles. Addresses
     * for ADPCM voices are nibble addresses. */
    aram[0] = 0;
    for (int i = 0; i < 7; ++i) aram[1 + i] = 0x12;
    aram[7] = 0xF1; /* include a negative nibble in the signed path */
    address.format = 0;
    address.endAddressLo = 15;
    address.currentAddressLo = 2;
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

    /* A looping ADPCM frame returns to its sample address and keeps running.
     * The loop history is restored before decoding the next frame. */
    memset(mixed, 0, sizeof(mixed));
    mixed_frames = 0;
    AXInit();
    voice = AXAcquireVoice(1, NULL, 0);
    assert(voice != NULL);
    address.loopFlag = 1;
    address.loopAddressLo = 2;
    address.endAddressLo = 15;
    address.currentAddressLo = 2;
    address.format = 0;
    AXSetVoiceAddr(voice, &address);
    AXSetVoiceAdpcm(voice, &(AXPBADPCM){ 0 });
    AXSetVoiceAdpcmLoop(voice, &(AXPBADPCMLOOP){ 0 });
    AXSetVoiceSrc(voice, &source);
    AXSetVoiceVe(voice, &envelope);
    AXSetVoiceMix(voice, &mix);
    AXSetVoiceState(voice, 1);
    NativeAudioTick();
    assert(mixed_frames == 533);
    assert(voice->pb.state != 0);
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
    address.format = 0x0A;
    address.endAddressLo = 1;
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
    address.format = 0x03;
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
