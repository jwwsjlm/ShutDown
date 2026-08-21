#pragma once

#include <QDateTime>
#include <QString>

struct PersistedTask {
    enum class Type { None, ScheduledAt, Countdown };
    Type type = Type::None;
    QDateTime target;
    qint64 remainingSeconds = 0;
    bool force = false;
    bool taskSchedulerFallback = false;
    bool paused = false;
};

class SettingsStore {
public:
    static void saveTask(const PersistedTask &task);
    static PersistedTask loadTask();
    static void clearTask();
    static bool hasTask();
};
