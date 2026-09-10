#include <assert.h>
#undef __assert
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/audio_output.h"
#include <sysdolphin/baselib/synth.c>

#define BLOCK_SIZE 0x1000
#define BLOCK_SAMPLES (BLOCK_SIZE / 16 * 14)
#define BLOCK_COUNT 3

static u8 hps[0x80 + BLOCK_COUNT * (0x20 + BLOCK_SIZE)];
static u8 aram[0x40000];
static const s16 block_values[BLOCK_COUNT][2] = { { 0x1234, -0x2345 },
                                                  { 0x3456, 0x4567 },
                                                  { -0x3456, 0x5678 } };
static bool finite;
static size_t output_frames;
static unsigned block_reads;
static u16 volume;
static u16 mix[2];

static void write_be16(u8* data, u16 value)
{
    data[0] = value >> 8;
    data[1] = value;
}

static void write_be32(u8* data, u32 value)
{
    data[0] = value >> 24;
    data[1] = value >> 16;
    data[2] = value >> 8;
    data[3] = value;
}

/* Predictor 0 repeats the previous sample. Each block starts with a distinct
 * history, so PCM checks detect a stale block or a swapped loop context. */
static void make_stream(void)
{
    memcpy(hps, " HALPST", 7);
    write_be32(hps + 8, 32000);
    write_be32(hps + 12, 2);
    for (int channel = 0; channel < 2; channel++) {
        u8* voice = hps + 0x10 + channel * 0x38;
        write_be16(voice, 1);
        write_be16(voice + 0x10, 0x800);
        write_be16(voice + 0x34, block_values[0][channel]);
    }
    for (int block = 0; block < BLOCK_COUNT; block++) {
        u8* header = hps + 0x80 + block * (0x20 + BLOCK_SIZE);
        u32 next = 0x80 + ((block + 1) % BLOCK_COUNT) * (0x20 + BLOCK_SIZE);
        write_be32(header, BLOCK_SIZE);
        write_be32(header + 4, BLOCK_SIZE - 1);
        if (finite && block == BLOCK_COUNT - 1) {
            next = -1U;
        }
        write_be32(header + 8, next);
        for (int channel = 0; channel < 2; channel++) {
            write_be16(header + 0xE + channel * 8,
                       block_values[block][channel]);
        }
    }
}

const unsigned char* NativeARAMPointer(u32 address, u32 length)
{
    assert(address <= sizeof(aram) && length <= sizeof(aram) - address);
    return aram + address;
}

bool NativeAudioOutputStart(uint32_t sample_rate)
{
    return sample_rate == 32000;
}

void NativeAudioOutputStop(void) {}
size_t NativeAudioOutputQueuedFrames(void)
{
    return 0;
}

size_t NativeAudioOutputSubmit(const int16_t* samples, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        size_t frame = output_frames + i;
        for (int channel = 0; channel < 2; channel++) {
            s32 expected = 0;
            if (frame >= 3 && (!finite || frame < BLOCK_COUNT * BLOCK_SAMPLES))
            {
                size_t block = ((frame - 3) / BLOCK_SAMPLES) % BLOCK_COUNT;
                expected = (block_values[block][channel] * volume) >> 15;
                expected = (expected * mix[channel]) >> 15;
            } else if (finite && frame >= BLOCK_COUNT * BLOCK_SAMPLES &&
                       frame < BLOCK_COUNT * BLOCK_SAMPLES + 160)
            {
                /* AX may still have its last SRC samples in this DSP block. */
                continue;
            }
            assert(samples[i * 2 + channel] == expected);
        }
    }
    output_frames += frames;
    return frames;
}

BOOL OSDisableInterrupts(void)
{
    return 1;
}
BOOL OSRestoreInterrupts(BOOL enabled)
{
    return enabled;
}

/* Keep I/O synchronous as in the native DVD and ARAM services. The borrowed
 * main-header buffer expires after its callback returns. */
int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size,
                      int type, int priority, HSD_DevComCallback callback,
                      void* args)
{
    (void) file;
    (void) priority;
    assert(src <= sizeof(hps) && size <= sizeof(hps) - src);
    if (type == 0x22) {
        u8* data = malloc(size);
        assert(data != NULL);
        memcpy(data, hps + src, size);
        callback(0, (intptr_t) args, data, false);
        free(data);
    } else if (type == 0x21) {
        assert(size == 0x20);
        block_reads++;
        memcpy((void*) dest, hps + src, size);
        callback(0, (intptr_t) args, NULL, false);
    } else {
        assert(type == 0x23);
        assert(dest <= sizeof(aram) && size <= sizeof(aram) - dest);
        memcpy(aram + dest, hps + src, size);
        callback(0, (intptr_t) args, NULL, false);
    }
    return 0;
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    finite = strcmp(argv[1], "finite") == 0;
    make_stream();
    AXInit();
    AXRegisterCallback(HSD_SynthCallback);
    HSD_Synth_804D7780 = 0x1000;
    HSD_Synth_804D7784 = 2;
    HSD_Synth_804C28E0_1784[0].x1784 = 1;
    HSD_Synth_804C28E0_1784[0].x1788 = 1;
    HSD_Synth_804D7754 = 1;
    int id = HSD_Synth_8038B5AC(0, 255, 255, 0);
    struct HSD_SynthSFXNode* node = getNode(id);
    assert(node != NULL && node->voice_count == 2 && node->x14 == 1);
    volume = node->voice[0]->pb.ve.currentVolume;
    mix[0] = node->voice[0]->pb.mix.vL;
    mix[1] = node->voice[1]->pb.mix.vR;
    assert(volume > 0 && mix[0] > 0 && mix[1] > 0);
    for (int i = 0; i < 120; i++) {
        NativeAudioTick();
    }
    assert(output_frames == 64000);
    assert(HSD_Synth_804D7774 < BLOCK_COUNT);
    if (finite) {
        assert(block_reads == BLOCK_COUNT);
        assert(getNode(id) == NULL);
    } else {
        assert(block_reads > 10);
        assert(getNode(id) != NULL && node->voice[0]->pb.state != 0);
    }
    return 0;
}
