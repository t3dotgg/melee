#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace melee::native {

    using AudioSampleId = std::uint32_t;
    using AudioVoiceId = std::uint64_t;

    // A backend-independent request for one sound effect. Durations are source
    // seconds; pitch scales playback speed while volume and pan are
    // normalized.
    struct AudioVoiceRequest {
        AudioSampleId sample_id = 0;
        double duration_seconds = 0.0;
        float volume = 1.0F;
        float pan = 0.0F;
        float pitch = 1.0F;
        std::uint8_t priority = 0;
    };

    enum class AudioCompletionReason : std::uint8_t {
        Finished,
        Stopped,
        Evicted
    };

    struct AudioVoice {
        AudioVoiceId id = 0;
        AudioVoiceRequest request;
        // Remaining wall-clock time, after applying request.pitch.
        double remaining_seconds = 0.0;
    };

    using AudioCompletion = std::function<void(AudioVoiceId, AudioSampleId,
                                               AudioCompletionReason)>;

    // Backends translate mixer events to a platform API (WASAPI, XAudio2,
    // etc.). The default NullAudioBackend deliberately emits no samples while
    // retaining deterministic lifecycle behavior for tests and headless
    // builds.
    class NativeAudioBackend {
    public:
        virtual ~NativeAudioBackend() = default;
        virtual void start(const AudioVoice& voice) = 0;
        virtual void stop(AudioVoiceId id, AudioCompletionReason reason) = 0;
        virtual void advance(double elapsed_seconds) = 0;
    };

    class NullAudioBackend final : public NativeAudioBackend {
    public:
        void start(const AudioVoice&) override {}
        void stop(AudioVoiceId, AudioCompletionReason) override {}
        void advance(double) override {}
    };

    struct AudioMixerConfig {
        std::size_t max_voices = 32;
    };

    class NativeAudioMixer final {
    public:
        explicit NativeAudioMixer(
            AudioMixerConfig config = {},
            std::shared_ptr<NativeAudioBackend> backend = nullptr);
        ~NativeAudioMixer() = default;

        NativeAudioMixer(const NativeAudioMixer&) = delete;
        NativeAudioMixer& operator=(const NativeAudioMixer&) = delete;

        // Starts a voice, returning nullopt when the priority limiter rejects
        // it. A higher-priority request evicts the oldest voice at the minimum
        // priority when the mixer is full.
        [[nodiscard]] std::optional<AudioVoiceId>
        play(AudioVoiceRequest request, AudioCompletion completion = {});
        // Stops one voice. Returns false when id is not active.
        bool stop(AudioVoiceId id);
        void stop_all();
        // Advances wall-clock time and dispatches completion callbacks exactly
        // once.
        void advance(double elapsed_seconds);

        [[nodiscard]] std::size_t active_count() const noexcept;
        [[nodiscard]] bool is_active(AudioVoiceId id) const noexcept;
        [[nodiscard]] std::vector<AudioVoice> active_voices() const;

    private:
        struct ActiveVoice {
            AudioVoice voice;
            AudioCompletion completion;
            std::uint64_t sequence = 0;
        };

        void validate(const AudioVoiceRequest& request) const;
        void complete(ActiveVoice voice, AudioCompletionReason reason,
                      std::vector<ActiveVoice>& callbacks);

        AudioMixerConfig config_;
        std::shared_ptr<NativeAudioBackend> backend_;
        std::vector<ActiveVoice> voices_;
        AudioVoiceId next_id_ = 1;
        std::uint64_t next_sequence_ = 1;
    };

} // namespace melee::native
