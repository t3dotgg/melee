// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <vector>

#include "Common/MacFrameTimer.h"

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#endif

using Clock = std::chrono::steady_clock;

static double ThreadSeconds()
{
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static void OriginalWait(Clock::time_point target)
{
    std::this_thread::sleep_until(target - std::chrono::microseconds{ 1020 });
    while (Clock::now() < target) {
        std::this_thread::yield();
    }
}

static void Measure(const char* name, void (*wait)(Clock::time_point), int fps)
{
    const int samples = fps * 5;
    std::vector<double> late;
    late.reserve(samples);
    const auto start = Clock::now();
    const double cpu_start = ThreadSeconds();
    for (int sample = 1; sample <= samples; ++sample) {
        const auto target =
            start + std::chrono::nanoseconds{ 1000000000LL * sample / fps };
        wait(target);
        const auto now = Clock::now();
        assert(now >= target);
        late.push_back(
            std::chrono::duration<double, std::micro>(now - target).count());
    }
    const double cpu_seconds = ThreadSeconds() - cpu_start;
    std::sort(late.begin(), late.end());
    std::printf("%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f\n", name, fps, samples,
                late[samples / 2], late[samples * 95 / 100],
                late[samples * 99 / 100], late.back(), cpu_seconds / 5 * 100);
}

int main()
{
#ifdef __OBJC__
    @autoreleasepool {
        id activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiated |
                                     NSActivityLatencyCritical
                              reason:@"Measure native frame timing"];
#endif
        Common::ConfigureMacFrameThread();
        std::puts("timer,fps,samples,p50_late_us,p95_late_us,p99_late_us,max_"
                  "late_us,cpu_percent");
        for (int fps : { 60, 120 }) {
            Measure("original", OriginalWait, fps);
            Measure("native", Common::SleepUntilMacFrame, fps);
        }
#ifdef __OBJC__
        [[NSProcessInfo processInfo] endActivity:activity];
    }
#endif
}
