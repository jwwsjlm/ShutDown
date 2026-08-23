#include "SettingsStore.h"
#include "SchedulerTimerPolicy.h"
#include "ShutdownScheduler.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <string>

int main() {
    wchar_t tempPath[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return 1;
    const std::wstring testAppData = std::wstring(tempPath) + L"ShutDownTests-" + std::to_wstring(GetCurrentProcessId());
    if (!CreateDirectoryW(testAppData.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    if (!SetEnvironmentVariableW(L"APPDATA", testAppData.c_str())) return 1;

    int failures = 0;
    auto fail = [&failures](const char *message) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; };
    SettingsStore::clearTask();
    const std::wstring settingsDirectory = testAppData + L"\\ShutDown";
    if (GetFileAttributesW(settingsDirectory.c_str()) != INVALID_FILE_ATTRIBUTES)
        fail("clearing missing settings must not create a directory");
    const auto missing = SettingsStore::loadTask();
    if (missing.type != PersistedTask::Type::None) fail("missing settings must load as empty");
    if (GetFileAttributesW(settingsDirectory.c_str()) != INVALID_FILE_ATTRIBUTES)
        fail("reading missing settings must not create a directory");
    PersistedTask original;
    original.type = PersistedTask::Type::ScheduledAt;
    original.targetEpoch = std::time(nullptr) + 3600;
    original.remainingSeconds = 3600;
    original.force = true; original.taskSchedulerFallback = true; original.paused = true;
    SettingsStore::saveTask(original);
    const auto loaded = SettingsStore::loadTask();
    if (loaded.type != original.type || loaded.targetEpoch != original.targetEpoch) fail("task round-trip");
    if (loaded.remainingSeconds != original.remainingSeconds || !loaded.force || !loaded.taskSchedulerFallback || !loaded.paused) fail("task flags round-trip");
    SettingsStore::clearTask();
    if (SettingsStore::hasTask()) fail("clear task");
    if (SchedulerTimerPolicy::intervalMs(ShutdownScheduler::State::Idle, 3600, false) != 0)
        fail("idle scheduler must not run a timer");
    if (SchedulerTimerPolicy::intervalMs(ShutdownScheduler::State::Paused, 3600, false) != 0)
        fail("paused scheduler must not run a timer");
    if (SchedulerTimerPolicy::intervalMs(ShutdownScheduler::State::Armed, 3600, true) != 1000)
        fail("visible countdown must refresh every second");
    if (SchedulerTimerPolicy::intervalMs(ShutdownScheduler::State::Armed, 3600, false) != 60000)
        fail("hidden long countdown must use a low-frequency timer");
    if (SchedulerTimerPolicy::intervalMs(ShutdownScheduler::State::Armed, 60, false) != 1000)
        fail("hidden countdown must regain one-second precision near shutdown");
    ShutdownScheduler scheduler;
    std::wstring error;
    int remainingNotifications = 0;
    scheduler.setRemainingCallback([&remainingNotifications](std::int64_t) { ++remainingNotifications; });
    // 空闲状态 tick 不应触发关机。
    scheduler.tick();
    if (scheduler.state() != ShutdownScheduler::State::Idle) fail("idle tick must remain idle");
    if (remainingNotifications != 0) fail("idle tick must not notify remaining time");
    if (!scheduler.scheduleCountdown(10, false, false, &error)) fail("schedule countdown");
    if (!scheduler.isActive() || scheduler.remainingSeconds() <= 0) fail("scheduler active");
    scheduler.pause(); if (scheduler.state() != ShutdownScheduler::State::Paused) fail("pause");
    const int notificationsBeforePausedTicks = remainingNotifications;
    for (int i = 0; i < 1000; ++i) scheduler.tick();
    if (remainingNotifications != notificationsBeforePausedTicks) fail("paused ticks must not emit duplicate updates");
    scheduler.resume(); if (scheduler.state() != ShutdownScheduler::State::Armed) fail("resume");
    scheduler.cancel(); if (scheduler.state() != ShutdownScheduler::State::Idle) fail("cancel");
    SettingsStore::clearTask();
    RemoveDirectoryW(settingsDirectory.c_str());
    RemoveDirectoryW(testAppData.c_str());
    if (failures == 0) { std::puts("Win32 scheduler tests: PASS"); return 0; }
    return 1;
}
