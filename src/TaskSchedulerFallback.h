#pragma once

#include <ctime>
#include <string>

class TaskSchedulerFallback {
public:
    static std::wstring taskName();
    static bool create(std::time_t when, bool force, std::wstring *errorMessage = nullptr);
    static bool remove(std::wstring *errorMessage = nullptr);
    static bool exists();
};
