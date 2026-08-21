#pragma once

#include "NativeTypes.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>

class ShutdownScheduler {
public:
    enum class State { Idle, Armed, Paused, Executing, Completed, Error };
    using StateCallback = std::function<void(State)>;
    using RemainingCallback = std::function<void(std::int64_t)>;
    using ErrorCallback = std::function<void(const std::wstring &)>;

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Armed || m_state == State::Paused; }
    std::int64_t remainingSeconds() const;

    bool scheduleAt(std::time_t target, bool force, bool fallback, std::wstring *errorMessage = nullptr);
    bool scheduleCountdown(std::int64_t seconds, bool force, bool fallback, std::wstring *errorMessage = nullptr);
    bool restore(const PersistedTask &task, std::wstring *errorMessage = nullptr);
    void cancel();
    void pause();
    void resume();
    void tick();

    void setStateCallback(StateCallback callback) { m_stateCallback = std::move(callback); }
    void setRemainingCallback(RemainingCallback callback) { m_remainingCallback = std::move(callback); }
    void setErrorCallback(ErrorCallback callback) { m_errorCallback = std::move(callback); }

private:
    bool arm(const PersistedTask &task, std::wstring *errorMessage);
    void setState(State state);
    void persist() const;

    State m_state = State::Idle;
    std::time_t m_target = 0;
    std::int64_t m_pausedRemaining = 0;
    bool m_force = false;
    bool m_fallback = false;
    PersistedTask::Type m_type = PersistedTask::Type::None;
    StateCallback m_stateCallback;
    RemainingCallback m_remainingCallback;
    ErrorCallback m_errorCallback;
};
