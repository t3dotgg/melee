// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace Metal {
    // Metal and Core Animation report seconds on the same host clock. Keep
    // callback work in memory. Write the CSV once, after the graphics backend
    // stops.
    class MeleeFrameLog {
    public:
        explicit MeleeFrameLog(std::string path, size_t capacity = 72000)
            : m_path(std::move(path)), m_capacity(capacity),
              m_samples(new Sample[capacity])
        {
        }

        static std::shared_ptr<MeleeFrameLog> FromEnvironment()
        {
            const char* path = std::getenv("MELEE_METAL_FRAME_LOG");
            return path && *path ? std::make_shared<MeleeFrameLog>(path)
                                 : nullptr;
        }

        // These three methods run on the render thread.
        size_t CurrentFrame() const
        {
            return m_frame;
        }
        bool CanRecord() const
        {
            return m_frame < m_capacity;
        }
        void NextFrame()
        {
            if (CanRecord()) {
                ++m_frame;
            }
        }

        void RecordPresent(size_t frame, double time)
        {
            if (frame >= m_capacity) {
                return;
            }
            std::lock_guard lock(m_mutex);
            m_samples[frame].submit = time;
        }

        void RecordDisplay(size_t frame, double time)
        {
            if (frame >= m_capacity) {
                return;
            }
            std::lock_guard lock(m_mutex);
            m_samples[frame].display = time;
        }

        void RecordGPU(size_t frame, double start, double end)
        {
            if (frame >= m_capacity || start <= 0 || end < start) {
                return;
            }
            std::lock_guard lock(m_mutex);
            auto& sample = m_samples[frame];
            if (sample.gpu_start == 0 || start < sample.gpu_start) {
                sample.gpu_start = start;
            }
            sample.gpu_end = std::max(sample.gpu_end, end);
            sample.gpu_busy_ms += (end - start) * 1000;
            ++sample.gpu_submissions;
        }

        bool Write()
        {
            std::lock_guard lock(m_mutex);
            std::FILE* file = std::fopen(m_path.c_str(), "w");
            if (!file) {
                std::perror("Melee Metal frame log");
                return false;
            }
            bool ok = std::fputs("frame,submit_s,gpu_start_s,gpu_end_s,gpu_"
                                 "busy_ms,display_s,"
                                 "gpu_submissions\n",
                                 file) >= 0;
            const size_t count = std::min(m_frame + 1, m_capacity);
            for (size_t frame = 0; frame < count; ++frame) {
                const auto& sample = m_samples[frame];
                if (sample.submit == 0 && sample.gpu_submissions == 0) {
                    continue;
                }
                ok &=
                    std::fprintf(file, "%zu,%.9f,%.9f,%.9f,%.6f,%.9f,%zu\n",
                                 frame, sample.submit, sample.gpu_start,
                                 sample.gpu_end, sample.gpu_busy_ms,
                                 sample.display, sample.gpu_submissions) >= 0;
            }
            ok &= std::fclose(file) == 0;
            if (!ok) {
                std::perror("Melee Metal frame log");
            }
            return ok;
        }

    private:
        struct Sample {
            double submit = 0;
            double gpu_start = 0;
            double gpu_end = 0;
            double gpu_busy_ms = 0;
            double display = 0;
            size_t gpu_submissions = 0;
        };

        const std::string m_path;
        const size_t m_capacity;
        std::unique_ptr<Sample[]> m_samples;
        std::mutex m_mutex;
        size_t m_frame = 0;
    };

    // Only the render thread replaces this pointer. Completion handlers keep
    // their own shared reference so they cannot access a destroyed graphics
    // backend.
    inline std::shared_ptr<MeleeFrameLog> g_melee_frame_log;
} // namespace Metal
