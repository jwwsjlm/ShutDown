#include "ShutdownScheduler.h"

#include "SettingsStore.h"
#include "ShutdownExecutor.h"
#include "TaskSchedulerFallback.h"

#include <algorithm>
#include <chrono>

namespace {
std::time_t now() { return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); }
std::time_t schedulerTime(std::time_t target) {
    std::tm local{};
    localtime_s(&local, &target);
    if (local.tm_sec == 0) return target;
    return target + (60 - local.tm_sec);
}
}

std::int64_t ShutdownScheduler::remainingSeconds() const {
    if (m_state == State::Paused) return m_pausedRemaining;
    if (!isActive()) return 0;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(m_target - now()));
}

bool ShutdownScheduler::scheduleAt(std::time_t target, bool force, bool fallback, std::wstring *errorMessage) {
    if (target <= now()) {
        if (errorMessage) *errorMessage = L"目标时间必须晚于当前时间";
        return false;
    }
    PersistedTask task;
    task.type = PersistedTask::Type::ScheduledAt;
    task.targetEpoch = target;
    task.remainingSeconds = target - now();
    task.force = force;
    task.taskSchedulerFallback = fallback;
    return arm(task, errorMessage);
}

bool ShutdownScheduler::scheduleCountdown(std::int64_t seconds, bool force, bool fallback, std::wstring *errorMessage) {
    if (seconds <= 0) {
        if (errorMessage) *errorMessage = L"倒计时必须大于 0 秒";
        return false;
    }
    PersistedTask task;
    task.type = PersistedTask::Type::Countdown;
    task.targetEpoch = now() + seconds;
    task.remainingSeconds = seconds;
    task.force = force;
    task.taskSchedulerFallback = fallback;
    return arm(task, errorMessage);
}

bool ShutdownScheduler::restore(const PersistedTask &task, std::wstring *errorMessage) {
    if (task.type != PersistedTask::Type::ScheduledAt && task.type != PersistedTask::Type::Countdown) {
        SettingsStore::clearTask();
        if (errorMessage) *errorMessage = L"持久化任务类型无效";
        return false;
    }
    if (task.targetEpoch <= now() && !task.paused) {
        SettingsStore::clearTask();
        if (errorMessage) *errorMessage = L"已忽略过期的关机任务";
        return false;
    }
    return arm(task, errorMessage);
}

bool ShutdownScheduler::arm(const PersistedTask &task, std::wstring *errorMessage) {
    if (isActive()) cancel();
    m_type = task.type;
    m_target = static_cast<std::time_t>(task.targetEpoch);
    m_pausedRemaining = task.remainingSeconds > 0 ? task.remainingSeconds : std::max<std::int64_t>(1, m_target - now());
    m_force = task.force;
    m_fallback = task.taskSchedulerFallback;
    if (m_fallback && !task.paused && !TaskSchedulerFallback::create(schedulerTime(m_target), m_force, errorMessage)) {
        m_type = PersistedTask::Type::None;
        return false;
    }
    setState(task.paused ? State::Paused : State::Armed);
    persist();
    if (m_remainingCallback) m_remainingCallback(remainingSeconds());
    return true;
}

void ShutdownScheduler::cancel() {
    if (m_fallback) TaskSchedulerFallback::remove(nullptr);
    m_type = PersistedTask::Type::None;
    m_target = 0;
    m_pausedRemaining = 0;
    m_force = false;
    m_fallback = false;
    SettingsStore::clearTask();
    setState(State::Idle);
    if (m_remainingCallback) m_remainingCallback(0);
}

void ShutdownScheduler::pause() {
    if (!isActive() || m_state == State::Paused) return;
    m_pausedRemaining = remainingSeconds();
    if (m_fallback) TaskSchedulerFallback::remove(nullptr);
    setState(State::Paused);
    persist();
}

void ShutdownScheduler::resume() {
    if (m_state != State::Paused) return;
    m_target = now() + m_pausedRemaining;
    if (m_fallback) {
        std::wstring error;
        if (!TaskSchedulerFallback::create(schedulerTime(m_target), m_force, &error)) {
            setState(State::Error);
            if (m_errorCallback) m_errorCallback(error);
            return;
        }
    }
    setState(State::Armed);
    persist();
}

void ShutdownScheduler::tick() {
    if (m_state == State::Paused) {
        if (m_remainingCallback) m_remainingCallback(m_pausedRemaining);
        return;
    }
    const auto remaining = remainingSeconds();
    if (m_remainingCallback) m_remainingCallback(remaining);
    if (remaining > 0) return;
    setState(State::Executing);
    std::wstring error;
    if (ShutdownExecutor::execute(m_force, &error)) {
        SettingsStore::clearTask();
        if (m_fallback) TaskSchedulerFallback::remove(nullptr);
        setState(State::Completed);
    } else {
        setState(State::Error);
        if (m_errorCallback) m_errorCallback(error);
        persist();
    }
}

void ShutdownScheduler::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    if (m_stateCallback) m_stateCallback(state);
}

void ShutdownScheduler::persist() const {
    if (m_type == PersistedTask::Type::None) return;
    PersistedTask task;
    task.type = m_type;
    task.targetEpoch = m_target;
    task.remainingSeconds = remainingSeconds();
    task.force = m_force;
    task.taskSchedulerFallback = m_fallback;
    task.paused = m_state == State::Paused;
    SettingsStore::saveTask(task);
}
