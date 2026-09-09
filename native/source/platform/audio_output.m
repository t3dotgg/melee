#include "audio_output.h"

#include <pthread.h>
#include <string.h>

#import <AudioToolbox/AudioToolbox.h>

enum {
    NATIVE_AUDIO_CHANNELS = 2,
    NATIVE_AUDIO_CAPACITY = 48000 * 2
};

static AudioUnit s_unit;
static bool s_running;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static int16_t s_ring[NATIVE_AUDIO_CAPACITY * NATIVE_AUDIO_CHANNELS];
static size_t s_read;
static size_t s_write;
static size_t s_count;

static OSStatus render_callback(void* refcon,
                                AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* timestamp, UInt32 bus,
                                UInt32 frames, AudioBufferList* buffers)
{
    (void) refcon;
    (void) flags;
    (void) timestamp;
    (void) bus;
    if (buffers == NULL || buffers->mNumberBuffers == 0) {
        return noErr;
    }

    AudioBuffer* output = &buffers->mBuffers[0];
    int16_t* destination = (int16_t*) output->mData;
    const size_t wanted = (size_t) frames;
    pthread_mutex_lock(&s_lock);
    size_t available = s_count < wanted ? s_count : wanted;
    for (size_t i = 0; i < available; ++i) {
        size_t index = s_read * NATIVE_AUDIO_CHANNELS;
        destination[i * NATIVE_AUDIO_CHANNELS] = s_ring[index];
        destination[i * NATIVE_AUDIO_CHANNELS + 1] = s_ring[index + 1];
        s_read = (s_read + 1) % NATIVE_AUDIO_CAPACITY;
    }
    s_count -= available;
    pthread_mutex_unlock(&s_lock);
    if (available < wanted) {
        memset(destination + available * NATIVE_AUDIO_CHANNELS, 0,
               (wanted - available) * NATIVE_AUDIO_CHANNELS *
                   sizeof(*destination));
    }
    output->mDataByteSize =
        (UInt32) (wanted * NATIVE_AUDIO_CHANNELS * sizeof(*destination));
    return noErr;
}

bool NativeAudioOutputStart(uint32_t sample_rate)
{
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    AudioComponent component;
    AudioStreamBasicDescription format;

    if (sample_rate == 0 || s_running) {
        return s_running;
    }
    component = AudioComponentFindNext(NULL, &description);
    if (component == NULL ||
        AudioComponentInstanceNew(component, &s_unit) != noErr)
    {
        s_unit = NULL;
        return false;
    }
    /* DefaultOutput already has output enabled. EnableIO belongs to HALOutput.
     */
    memset(&format, 0, sizeof(format));
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsSignedInteger |
                          kAudioFormatFlagIsPacked |
                          kAudioFormatFlagsNativeEndian;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = NATIVE_AUDIO_CHANNELS;
    format.mBitsPerChannel = 16;
    format.mBytesPerFrame = NATIVE_AUDIO_CHANNELS * sizeof(int16_t);
    format.mBytesPerPacket = format.mBytesPerFrame;
    if (AudioUnitSetProperty(s_unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &format,
                             sizeof(format)) != noErr)
    {
        AudioComponentInstanceDispose(s_unit);
        s_unit = NULL;
        return false;
    }
    pthread_mutex_lock(&s_lock);
    s_read = s_write = s_count = 0;
    pthread_mutex_unlock(&s_lock);
    AURenderCallbackStruct callback = { render_callback, NULL };
    if (AudioUnitSetProperty(s_unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &callback,
                             sizeof(callback)) != noErr ||
        AudioUnitInitialize(s_unit) != noErr ||
        AudioOutputUnitStart(s_unit) != noErr)
    {
        AudioComponentInstanceDispose(s_unit);
        s_unit = NULL;
        return false;
    }
    s_running = true;
    return true;
}

void NativeAudioOutputStop(void)
{
    if (!s_running) {
        return;
    }
    AudioOutputUnitStop(s_unit);
    AudioUnitUninitialize(s_unit);
    AudioComponentInstanceDispose(s_unit);
    s_unit = NULL;
    s_running = false;
    pthread_mutex_lock(&s_lock);
    s_read = s_write = s_count = 0;
    pthread_mutex_unlock(&s_lock);
}

size_t NativeAudioOutputSubmit(const int16_t* samples, size_t frames)
{
    if (!s_running || samples == NULL || frames == 0) {
        return 0;
    }
    pthread_mutex_lock(&s_lock);
    size_t accepted = frames;
    if (accepted > NATIVE_AUDIO_CAPACITY - s_count) {
        accepted = NATIVE_AUDIO_CAPACITY - s_count;
    }
    for (size_t i = 0; i < accepted; ++i) {
        size_t index = s_write * NATIVE_AUDIO_CHANNELS;
        s_ring[index] = samples[i * NATIVE_AUDIO_CHANNELS];
        s_ring[index + 1] = samples[i * NATIVE_AUDIO_CHANNELS + 1];
        s_write = (s_write + 1) % NATIVE_AUDIO_CAPACITY;
    }
    s_count += accepted;
    pthread_mutex_unlock(&s_lock);
    return accepted;
}

size_t NativeAudioOutputQueuedFrames(void)
{
    size_t count;
    pthread_mutex_lock(&s_lock);
    count = s_count;
    pthread_mutex_unlock(&s_lock);
    return count;
}
