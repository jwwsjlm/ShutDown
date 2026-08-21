#pragma once

#include "SettingsStore.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

class ShutdownScheduler final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Armed, Paused, Executing, Completed, Error };
    Q_ENUM(State)

    explicit ShutdownScheduler(QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Armed || m_state == State::Paused; }
    QDateTime target() const { return m_target; }
    qint64 remainingSeconds() const;
    bool force() const { return m_force; }
    bool fallbackEnabled() const { return m_fallback; }

    bool scheduleAt(const QDateTime &target, bool force, bool fallback, QString *errorMessage = nullptr);
    bool scheduleCountdown(qint64 seconds, bool force, bool fallback, QString *errorMessage = nullptr);
    bool restore(const PersistedTask &task, QString *errorMessage = nullptr);

public slots:
    void cancel();
    void pause();
    void resume();

signals:
    void stateChanged(ShutdownScheduler::State state);
    void remainingChanged(qint64 seconds);
    void completed();
    void executionError(const QString &message);

private slots:
    void tick();

private:
    bool arm(const PersistedTask &task, QString *errorMessage);
    void setState(State state);
    void persist() const;

    QTimer m_timer;
    State m_state = State::Idle;
    PersistedTask::Type m_type = PersistedTask::Type::None;
    QDateTime m_target;
    qint64 m_pausedRemaining = 0;
    bool m_force = false;
    bool m_fallback = false;
};
