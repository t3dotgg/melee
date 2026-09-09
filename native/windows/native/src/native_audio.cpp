#include "native_audio.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace melee::native {

    NativeAudioMixer::NativeAudioMixer(
        AudioMixerConfig config, std::shared_ptr<NativeAudioBackend> backend)
        : config_(config), backend_(std::move(backend))
    {
        if (config_.max_voices == 0) {
            throw std::invalid_argument("max_voices must be nonzero");
        }
        if (!backend_) {
            backend_ = std::make_shared<NullAudioBackend>();
        }
        voices_.reserve(config_.max_voices);
    }

    void NativeAudioMixer::validate(const AudioVoiceRequest& request) const
    {
        if (!std::isfinite(request.duration_seconds) ||
            request.duration_seconds < 0.0)
        {
            throw std::invalid_argument(
                "audio duration must be finite and nonnegative");
        }
        if (!std::isfinite(request.volume) || request.volume < 0.0F ||
            request.volume > 1.0F)
        {
            throw std::invalid_argument(
                "audio volume must be finite and in [0,1]");
        }
        if (!std::isfinite(request.pan) || request.pan < -1.0F ||
            request.pan > 1.0F)
        {
            throw std::invalid_argument(
                "audio pan must be finite and in [-1,1]");
        }
        if (!std::isfinite(request.pitch) || request.pitch <= 0.0F) {
            throw std::invalid_argument(
                "audio pitch must be finite and positive");
        }
    }

    std::optional<AudioVoiceId>
    NativeAudioMixer::play(AudioVoiceRequest request,
                           AudioCompletion completion)
    {
        validate(request);

        std::vector<ActiveVoice> callbacks;
        if (voices_.size() >= config_.max_voices) {
            const auto minimum = std::min_element(
                voices_.begin(), voices_.end(),
                [](const ActiveVoice& lhs, const ActiveVoice& rhs) {
                    if (lhs.voice.request.priority !=
                        rhs.voice.request.priority) {
                        return lhs.voice.request.priority <
                               rhs.voice.request.priority;
                    }
                    return lhs.sequence < rhs.sequence;
                });
            if (minimum == voices_.end() ||
                request.priority <= minimum->voice.request.priority)
            {
                return std::nullopt;
            }
            complete(std::move(*minimum), AudioCompletionReason::Evicted,
                     callbacks);
            voices_.erase(minimum);
        }

        ActiveVoice active;
        active.voice.id = next_id_++;
        // Reserve zero as an invalid handle if the counter ever wraps.
        if (next_id_ == 0) {
            next_id_ = 1;
        }
        active.voice.request = request;
        active.voice.remaining_seconds =
            request.duration_seconds / static_cast<double>(request.pitch);
        active.completion = std::move(completion);
        active.sequence = next_sequence_++;
        if (next_sequence_ == 0) {
            next_sequence_ = 1;
        }
        const auto id = active.voice.id;
        backend_->start(active.voice);
        voices_.push_back(std::move(active));

        // Eviction callbacks are delivered after the new voice is visible to a
        // re-entrant callback, while no internal state is locked.
        for (auto& callback : callbacks) {
            if (callback.completion) {
                callback.completion(callback.voice.id,
                                    callback.voice.request.sample_id,
                                    AudioCompletionReason::Evicted);
            }
        }
        return id;
    }

    void NativeAudioMixer::complete(ActiveVoice voice,
                                    AudioCompletionReason reason,
                                    std::vector<ActiveVoice>& callbacks)
    {
        backend_->stop(voice.voice.id, reason);
        if (voice.completion) {
            callbacks.push_back(std::move(voice));
        }
    }

    bool NativeAudioMixer::stop(AudioVoiceId id)
    {
        const auto found = std::find_if(
            voices_.begin(), voices_.end(),
            [id](const ActiveVoice& voice) { return voice.voice.id == id; });
        if (found == voices_.end()) {
            return false;
        }
        auto voice = std::move(*found);
        voices_.erase(found);
        backend_->stop(id, AudioCompletionReason::Stopped);
        if (voice.completion) {
            voice.completion(id, voice.voice.request.sample_id,
                             AudioCompletionReason::Stopped);
        }
        return true;
    }

    void NativeAudioMixer::stop_all()
    {
        // Preserve insertion order to make callback ordering deterministic.
        auto voices = std::move(voices_);
        voices_.clear();
        for (auto& voice : voices) {
            backend_->stop(voice.voice.id, AudioCompletionReason::Stopped);
            if (voice.completion) {
                voice.completion(voice.voice.id, voice.voice.request.sample_id,
                                 AudioCompletionReason::Stopped);
            }
        }
    }

    void NativeAudioMixer::advance(double elapsed_seconds)
    {
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
            throw std::invalid_argument(
                "audio elapsed time must be finite and nonnegative");
        }

        backend_->advance(elapsed_seconds);
        std::vector<ActiveVoice> callbacks;
        for (auto iterator = voices_.begin(); iterator != voices_.end();) {
            iterator->voice.remaining_seconds -= elapsed_seconds;
            if (iterator->voice.remaining_seconds > 0.0) {
                ++iterator;
                continue;
            }
            auto finished = std::move(*iterator);
            iterator = voices_.erase(iterator);
            complete(std::move(finished), AudioCompletionReason::Finished,
                     callbacks);
        }
        for (auto& callback : callbacks) {
            callback.completion(callback.voice.id,
                                callback.voice.request.sample_id,
                                AudioCompletionReason::Finished);
        }
    }

    std::size_t NativeAudioMixer::active_count() const noexcept
    {
        return voices_.size();
    }

    bool NativeAudioMixer::is_active(AudioVoiceId id) const noexcept
    {
        return std::any_of(
            voices_.begin(), voices_.end(),
            [id](const ActiveVoice& voice) { return voice.voice.id == id; });
    }

    std::vector<AudioVoice> NativeAudioMixer::active_voices() const
    {
        std::vector<AudioVoice> result;
        result.reserve(voices_.size());
        for (const auto& voice : voices_) {
            result.push_back(voice.voice);
        }
        return result;
    }

} // namespace melee::native
