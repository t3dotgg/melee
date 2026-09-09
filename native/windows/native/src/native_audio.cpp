#include "native_audio.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#    include <windows.h>
#    ifdef min
#        undef min
#    endif
#    ifdef max
#        undef max
#    endif
#endif

namespace melee::native {

#ifdef _WIN32
namespace {
    // Keep these declarations local instead of including mmsystem.h.  This
    // avoids a link-time dependency on winmm.lib and makes availability an
    // ordinary runtime decision.
    struct WaveFormat {
        std::uint16_t tag;
        std::uint16_t channels;
        std::uint32_t samples_per_second;
        std::uint32_t bytes_per_second;
        std::uint16_t block_align;
        std::uint16_t bits_per_sample;
        std::uint16_t extra_size;
    };
    struct WaveHeader {
        char* data;
        std::uint32_t buffer_length;
        std::uint32_t bytes_recorded;
        ULONG_PTR user;
        std::uint32_t flags;
        std::uint32_t loops;
        WaveHeader* next;
        ULONG_PTR reserved;
    };
    using WaveHandle = void*;
    using WaveOutOpen = UINT(WINAPI*)(WaveHandle*, UINT, const WaveFormat*,
                                      DWORD_PTR, DWORD_PTR, DWORD);
    using WaveOutClose = UINT(WINAPI*)(WaveHandle);
    using WaveOutPrepare = UINT(WINAPI*)(WaveHandle, WaveHeader*, UINT);
    using WaveOutUnprepare = UINT(WINAPI*)(WaveHandle, WaveHeader*, UINT);
    using WaveOutWrite = UINT(WINAPI*)(WaveHandle, WaveHeader*, UINT);
    using WaveOutReset = UINT(WINAPI*)(WaveHandle);

    constexpr UINT kWaveMapper = static_cast<UINT>(-1);
    constexpr std::uint16_t kPcmFormat = 1;
    constexpr double kSampleRate = 48000.0;
    constexpr double kMaxToneSeconds = 2.0;
}
#endif

struct NativeWindowsAudioBackend::State {
#ifdef _WIN32
    HMODULE module = nullptr;
    WaveOutOpen open = nullptr;
    WaveOutClose close = nullptr;
    WaveOutPrepare prepare = nullptr;
    WaveOutUnprepare unprepare = nullptr;
    WaveOutWrite write = nullptr;
    WaveOutReset reset = nullptr;
    struct VoiceBuffer {
        WaveHandle handle = nullptr;
        WaveHeader header{};
        std::vector<std::int16_t> pcm;
    };
    std::map<AudioVoiceId, VoiceBuffer> voices;
    std::mutex mutex;
#endif
    bool available = false;
};

NativeWindowsAudioBackend::NativeWindowsAudioBackend()
    : state_(std::make_unique<State>())
{
#ifdef _WIN32
    state_->module = LoadLibraryW(L"winmm.dll");
    if (state_->module == nullptr) {
        return;
    }
    state_->open = reinterpret_cast<WaveOutOpen>(
        GetProcAddress(state_->module, "waveOutOpen"));
    state_->close = reinterpret_cast<WaveOutClose>(
        GetProcAddress(state_->module, "waveOutClose"));
    state_->prepare = reinterpret_cast<WaveOutPrepare>(
        GetProcAddress(state_->module, "waveOutPrepareHeader"));
    state_->unprepare = reinterpret_cast<WaveOutUnprepare>(
        GetProcAddress(state_->module, "waveOutUnprepareHeader"));
    state_->write = reinterpret_cast<WaveOutWrite>(
        GetProcAddress(state_->module, "waveOutWrite"));
    state_->reset = reinterpret_cast<WaveOutReset>(
        GetProcAddress(state_->module, "waveOutReset"));
    state_->available = state_->open != nullptr && state_->close != nullptr &&
                        state_->prepare != nullptr &&
                        state_->unprepare != nullptr && state_->write != nullptr &&
                        state_->reset != nullptr;
#endif
}

NativeWindowsAudioBackend::~NativeWindowsAudioBackend()
{
#ifdef _WIN32
    if (state_ != nullptr) {
        std::lock_guard lock(state_->mutex);
        for (auto& [id, voice] : state_->voices) {
            (void) id;
            state_->reset(voice.handle);
            state_->unprepare(voice.handle, &voice.header,
                              static_cast<UINT>(sizeof(WaveHeader)));
            state_->close(voice.handle);
        }
        state_->voices.clear();
        if (state_->module != nullptr) {
            FreeLibrary(state_->module);
        }
    }
#endif
}

bool NativeWindowsAudioBackend::available() const noexcept
{
    return state_ != nullptr && state_->available;
}

void NativeWindowsAudioBackend::start(const AudioVoice& voice)
{
#ifdef _WIN32
    if (!available() || voice.request.duration_seconds <= 0.0) {
        return;
    }
    const auto seconds = std::min(voice.request.duration_seconds,
                                  kMaxToneSeconds);
    const auto frames = static_cast<std::size_t>(
        std::max(1.0, std::ceil(seconds * kSampleRate)));
    State::VoiceBuffer buffer;
    buffer.pcm.resize(frames * 2);
    const auto frequency =
        (220.0 + static_cast<double>(voice.request.sample_id % 32U) * 17.0) *
        static_cast<double>(voice.request.pitch);
    const auto left = voice.request.volume *
                      (voice.request.pan > 0.0F ? 1.0F - voice.request.pan
                                                : 1.0F);
    const auto right = voice.request.volume *
                       (voice.request.pan < 0.0F ? 1.0F + voice.request.pan
                                                 : 1.0F);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(
            std::sin(2.0 * 3.141592653589793 * frequency *
                     static_cast<double>(frame) / kSampleRate) * 0.18);
        buffer.pcm[frame * 2] = static_cast<std::int16_t>(
            std::lround(value * left * 32767.0F));
        buffer.pcm[frame * 2 + 1] = static_cast<std::int16_t>(
            std::lround(value * right * 32767.0F));
    }
    buffer.header.data = reinterpret_cast<char*>(buffer.pcm.data());
    buffer.header.buffer_length =
        static_cast<std::uint32_t>(buffer.pcm.size() * sizeof(std::int16_t));
    WaveFormat format{ kPcmFormat, 2, static_cast<std::uint32_t>(kSampleRate),
                       static_cast<std::uint32_t>(kSampleRate * 4), 4, 16,
                       0 };
    if (state_->open(&buffer.handle, kWaveMapper, &format, 0, 0, 0) != 0) {
        return;
    }
    std::lock_guard lock(state_->mutex);
    // Insert before handing the header to waveOut: waveOut retains this
    // pointer until playback completes, so a stack or moved-from header would
    // be invalid while the device thread is active.
    auto [iterator, inserted] = state_->voices.emplace(voice.id,
                                                        std::move(buffer));
    if (!inserted) {
        state_->close(iterator->second.handle);
        return;
    }
    auto& queued = iterator->second;
    const auto prepared = state_->prepare(
        queued.handle, &queued.header, static_cast<UINT>(sizeof(WaveHeader)));
    const auto written = prepared == 0
                             ? state_->write(queued.handle, &queued.header,
                                             static_cast<UINT>(sizeof(WaveHeader)))
                             : 1U;
    if (prepared != 0 || written != 0) {
        state_->reset(queued.handle);
        if (prepared == 0) {
            state_->unprepare(queued.handle, &queued.header,
                              static_cast<UINT>(sizeof(WaveHeader)));
        }
        state_->close(queued.handle);
        state_->voices.erase(iterator);
    }
#else
    (void) voice;
#endif
}

void NativeWindowsAudioBackend::stop(AudioVoiceId id,
                                     AudioCompletionReason reason)
{
    (void) reason;
#ifdef _WIN32
    if (!available()) {
        return;
    }
    std::lock_guard lock(state_->mutex);
    const auto found = state_->voices.find(id);
    if (found == state_->voices.end()) {
        return;
    }
    auto& voice = found->second;
    state_->reset(voice.handle);
    state_->unprepare(voice.handle, &voice.header,
                      static_cast<UINT>(sizeof(WaveHeader)));
    state_->close(voice.handle);
    state_->voices.erase(found);
#else
    (void) id;
#endif
}

void NativeWindowsAudioBackend::advance(double elapsed_seconds)
{
    // NativeAudioMixer owns deterministic voice expiry.  waveOut runs on its
    // own worker thread, so no polling is needed here.
    (void) elapsed_seconds;
}

std::shared_ptr<NativeAudioBackend> make_platform_audio_backend()
{
    auto backend = std::make_shared<NativeWindowsAudioBackend>();
    if (backend->available()) {
        return backend;
    }
    return std::make_shared<NullAudioBackend>();
}

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
