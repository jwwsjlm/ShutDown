#include "ShutdownScheduler.h"

#include "ShutdownExecutor.h"
#include "TaskSchedulerFallback.h"

#include <QDateTime>
#include <QDebug>

namespace {
QDateTime taskSchedulerTime(const QDateTime &target) {
    const int seconds = target.time().second();
    return seconds == 0 ? target : target.addSecs(60 - seconds);
}
}

ShutdownScheduler::ShutdownScheduler(QObject *parent) : QObject(parent) {
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &ShutdownScheduler::tick);
}

qint64 ShutdownScheduler::remainingSeconds() const {
    if (m_state == State::Paused) return m_pausedRemaining;
    if (!isActive()) return 0;
    return qMax<qint64>(0, QDateTime::currentDateTime().secsTo(m_target));
}

bool ShutdownScheduler::scheduleAt(const QDateTime &target, bool force, bool fallback, QString *errorMessage) {
    if (!target.isValid() || target <= QDateTime::currentDateTime()) {
        if (errorMessage) *errorMessage = QStringLiteral("目标时间必须晚于当前时间");
        return false;
    }
    PersistedTask task;
    task.type = PersistedTask::Type::ScheduledAt;
    task.target = target;
    task.force = force;
    task.taskSchedulerFallback = fallback;
    return arm(task, errorMessage);
}

bool ShutdownScheduler::scheduleCountdown(qint64 seconds, bool force, bool fallback, QString *errorMessage) {
    if (seconds <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("倒计时必须大于 0 秒");
        return false;
    }
    PersistedTask task;
    task.type = PersistedTask::Type::Countdown;
    task.target = QDateTime::currentDateTime().addSecs(seconds);
    task.remainingSeconds = seconds;
    task.force = force;
    task.taskSchedulerFallback = fallback;
    return arm(task, errorMessage);
}

bool ShutdownScheduler::restore(const PersistedTask &task, QString *errorMessage) {
    if (task.type != PersistedTask::Type::ScheduledAt && task.type != PersistedTask::Type::Countdown) {
        if (errorMessage) *errorMessage = QStringLiteral("持久化任务类型无效");
        SettingsStore::clearTask();
        return false;
    }
    if (task.type == PersistedTask::Type::ScheduledAt && task.target <= QDateTime::currentDateTime()) {
        SettingsStore::clearTask();
        if (errorMessage) *errorMessage = QStringLiteral("已忽略过期的关机任务");
        return false;
    }
    return arm(task, errorMessage);
}

bool ShutdownScheduler::arm(const PersistedTask &task, QString *errorMessage) {
    if (isActive()) cancel();
    m_type = task.type;
    m_target = task.target;
    m_force = task.force;
    m_fallback = task.taskSchedulerFallback;
    m_pausedRemaining = task.remainingSeconds > 0 ? task.remainingSeconds : qMax<qint64>(1, QDateTime::currentDateTime().secsTo(m_target));
    if (m_fallback && !task.paused && !TaskSchedulerFallback::create(taskSchedulerTime(m_target), m_force, errorMessage)) {
        m_type = PersistedTask::Type::None;
        m_target = {};
        m_pausedRemaining = 0;
        m_force = false;
        m_fallback = false;
        return false;
    }
    qInfo() << "Shutdown task armed for" << m_target << "force=" << m_force << "fallback=" << m_fallback << "paused=" << task.paused;
    setState(task.paused ? State::Paused : State::Armed);
    persist();
    m_timer.start();
    emit remainingChanged(remainingSeconds());
    return true;
}

void ShutdownScheduler::cancel() {
    qInfo() << "Shutdown task cancelled";
    m_timer.stop();
    if (m_fallback) TaskSchedulerFallback::remove(nullptr);
    m_type = PersistedTask::Type::None;
    m_target = {};
    m_pausedRemaining = 0;
    m_force = false;
    m_fallback = false;
    SettingsStore::clearTask();
    setState(State::Idle);
    emit remainingChanged(0);
}

void ShutdownScheduler::pause() {
    if (!isActive() || m_state == State::Paused) return;
    m_pausedRemaining = remainingSeconds();
    if (m_fallback) TaskSchedulerFallback::remove(nullptr);
    qInfo() << "Shutdown task paused with" << m_pausedRemaining << "seconds remaining";
    setState(State::Paused);
    persist();
}

void ShutdownScheduler::resume() {
    if (m_state != State::Paused) return;
    m_target = QDateTime::currentDateTime().addSecs(m_pausedRemaining);
    if (m_fallback) {
        QString error;
        if (!TaskSchedulerFallback::create(taskSchedulerTime(m_target), m_force, &error)) {
            setState(State::Error);
            emit executionError(error);
            persist();
            return;
        }
    }
    setState(State::Armed);
    qInfo() << "Shutdown task resumed for" << m_target;
    persist();
}

void ShutdownScheduler::tick() {
    if (m_state == State::Paused) {
        emit remainingChanged(m_pausedRemaining);
        return;
    }
    const qint64 remaining = remainingSeconds();
    emit remainingChanged(remaining);
    if (remaining > 0) return;
    m_timer.stop();
    setState(State::Executing);
    QString error;
    if (ShutdownExecutor::execute(m_force, &error)) {
        qInfo() << "Shutdown execution accepted by the operating system";
        SettingsStore::clearTask();
        if (m_fallback) TaskSchedulerFallback::remove(nullptr);
        setState(State::Completed);
        emit completed();
    } else {
        qCritical() << "Shutdown execution failed:" << error;
        setState(State::Error);
        emit executionError(error);
        persist();
    }
}

void ShutdownScheduler::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

void ShutdownScheduler::persist() const {
    if (m_type == PersistedTask::Type::None) return;
    PersistedTask task;
    task.type = m_type;
    task.target = m_target;
    task.remainingSeconds = remainingSeconds();
    task.force = m_force;
    task.taskSchedulerFallback = m_fallback;
    task.paused = m_state == State::Paused;
    SettingsStore::saveTask(task);
}
