#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "native_audio.h"

namespace {
    using namespace melee::native;

    struct RecordingBackend final : NativeAudioBackend {
        std::vector<AudioVoiceId> started;
        std::vector<std::pair<AudioVoiceId, AudioCompletionReason>> stopped;
        double advanced = 0.0;

        void start(const AudioVoice& voice) override
        {
            started.push_back(voice.id);
        }
        void stop(AudioVoiceId id, AudioCompletionReason reason) override
        {
            stopped.emplace_back(id, reason);
        }
        void advance(double seconds) override
        {
            advanced += seconds;
        }
    };
} // namespace

int main()
{
    using namespace melee::native;

    bool threw = false;
    try {
        NativeAudioMixer invalid(AudioMixerConfig{ .max_voices = 0 });
        (void) invalid;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    auto backend = std::make_shared<RecordingBackend>();
    NativeAudioMixer mixer(AudioMixerConfig{ .max_voices = 2 }, backend);
    std::vector<AudioCompletionReason> reasons;
    auto callback = [&](AudioVoiceId, AudioSampleId sample,
                        AudioCompletionReason reason) {
        assert(sample != 0);
        reasons.push_back(reason);
    };

    const auto first = mixer.play(AudioVoiceRequest{ .sample_id = 10,
                                                     .duration_seconds = 1.0,
                                                     .volume = 0.8F,
                                                     .pan = -0.25F,
                                                     .pitch = 2.0F,
                                                     .priority = 1 },
                                  callback);
    assert(first.has_value() && mixer.active_count() == 1);
    assert(std::abs(mixer.active_voices()[0].remaining_seconds - 0.5) < 1e-12);

    const auto second = mixer.play(AudioVoiceRequest{ .sample_id = 11,
                                                      .duration_seconds = 2.0,
                                                      .priority = 3 },
                                   callback);
    assert(second.has_value() && mixer.active_count() == 2);

    // Equal/lower priority cannot displace an active voice.
    assert(!mixer.play(AudioVoiceRequest{ .sample_id = 12,
                                          .duration_seconds = 1.0,
                                          .priority = 1 },
                       callback));
    assert(mixer.active_count() == 2);

    // A higher priority request evicts the oldest minimum-priority voice.
    const auto third = mixer.play(AudioVoiceRequest{ .sample_id = 13,
                                                     .duration_seconds = 1.0,
                                                     .priority = 4 },
                                  callback);
    assert(third.has_value() && mixer.active_count() == 2);
    assert(!mixer.is_active(*first));
    assert(reasons.size() == 1 &&
           reasons[0] == AudioCompletionReason::Evicted);
    assert(backend->stopped.size() == 1);
    assert(backend->stopped[0].second == AudioCompletionReason::Evicted);

    mixer.advance(0.49);
    assert(mixer.active_count() == 2);
    mixer.advance(0.01);
    assert(mixer.active_count() == 1);
    assert(reasons.size() == 2 &&
           reasons[1] == AudioCompletionReason::Finished);
    assert(backend->advanced == 0.5);

    assert(mixer.stop(*third));
    assert(mixer.active_count() == 0);
    assert(reasons.size() == 3 &&
           reasons[2] == AudioCompletionReason::Stopped);
    assert(!mixer.stop(*third));

    (void) mixer.play(
        AudioVoiceRequest{ .sample_id = 14, .duration_seconds = 10.0 },
        callback);
    (void) mixer.play(
        AudioVoiceRequest{ .sample_id = 15, .duration_seconds = 10.0 },
        callback);
    mixer.stop_all();
    assert(mixer.active_count() == 0);
    assert(reasons.size() == 5);
    assert(reasons[3] == AudioCompletionReason::Stopped &&
           reasons[4] == AudioCompletionReason::Stopped);

    threw = false;
    try {
        (void) mixer.play(AudioVoiceRequest{ .duration_seconds = -1.0 });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        mixer.advance(-0.1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    std::cout << "native audio tests passed\n";
}
