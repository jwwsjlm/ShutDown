#pragma once

#include "ShutdownScheduler.h"

#include <cstdint>

namespace SchedulerTimerPolicy {
constexpr std::uint32_t kVisibleIntervalMs = 1000;
constexpr std::uint32_t kHiddenIntervalMs = 60000;

constexpr std::uint32_t intervalMs(ShutdownScheduler::State state,
                                   std::int64_t remainingSeconds,
                                   bool windowVisible) {
    if (state != ShutdownScheduler::State::Armed) return 0;
    return windowVisible || remainingSeconds <= 60 ? kVisibleIntervalMs : kHiddenIntervalMs;
}
}
