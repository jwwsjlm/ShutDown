#include "SettingsStore.h"
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
    ShutdownScheduler scheduler;
    std::wstring error;
    // 空闲状态 tick 不应触发关机。
    scheduler.tick();
    if (scheduler.state() != ShutdownScheduler::State::Idle) fail("idle tick must remain idle");
    if (!scheduler.scheduleCountdown(10, false, false, &error)) fail("schedule countdown");
    if (!scheduler.isActive() || scheduler.remainingSeconds() <= 0) fail("scheduler active");
    scheduler.pause(); if (scheduler.state() != ShutdownScheduler::State::Paused) fail("pause");
    scheduler.resume(); if (scheduler.state() != ShutdownScheduler::State::Armed) fail("resume");
    scheduler.cancel(); if (scheduler.state() != ShutdownScheduler::State::Idle) fail("cancel");
    SettingsStore::clearTask();
    RemoveDirectoryW((testAppData + L"\\ShutDown").c_str());
    RemoveDirectoryW(testAppData.c_str());
    if (failures == 0) { std::puts("Win32 scheduler tests: PASS"); return 0; }
    return 1;
}
