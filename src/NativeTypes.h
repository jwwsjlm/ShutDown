#pragma once

#include <cstdint>

struct PersistedTask {
    enum class Type { None, ScheduledAt, Countdown };
    Type type = Type::None;
    std::int64_t targetEpoch = 0;
    std::int64_t remainingSeconds = 0;
    bool force = false;
    bool taskSchedulerFallback = false;
    bool paused = false;
};
