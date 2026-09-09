#ifndef MELEE_NATIVE_PLATFORM_AUDIO_OUTPUT_H
#define MELEE_NATIVE_PLATFORM_AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A small host output queue for interleaved signed 16-bit PCM. The queue is
 * optional, so headless runs can leave it stopped while the game still uses
 * the Dolphin AI state API. */
bool NativeAudioOutputStart(uint32_t sample_rate);
void NativeAudioOutputStop(void);
size_t NativeAudioOutputSubmit(const int16_t* samples, size_t frames);
size_t NativeAudioOutputQueuedFrames(void);

/* Render elapsed 160-frame AX blocks for one 60 Hz video retrace. */
void NativeAudioTick(void);

#endif
