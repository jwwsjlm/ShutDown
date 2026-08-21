#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PersistedTask {
    enum class Type { None, ScheduledAt, Countdown };
    Type type = Type::None;
    std::int64_t targetEpoch = 0;
    std::int64_t remainingSeconds = 0;
    bool force = false;
    bool taskSchedulerFallback = false;
    bool paused = false;
};

struct UpdateInfo {
    std::string version;
    std::string tagName;
    std::string title;
    std::string notes;
    std::string assetName;
    std::string downloadUrl;
    std::vector<std::string> mirrorUrls;
    std::vector<unsigned char> sha256;

    bool isValid() const { return !version.empty() && !downloadUrl.empty(); }
};
