#include "SettingsStore.h"

#include <QSettings>

namespace {
constexpr auto kGroup = "activeTask";
}

void SettingsStore::saveTask(const PersistedTask &task) {
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue("type", static_cast<int>(task.type));
    settings.setValue("target", task.target.toString(Qt::ISODateWithMs));
    settings.setValue("remainingSeconds", task.remainingSeconds);
    settings.setValue("force", task.force);
    settings.setValue("taskSchedulerFallback", task.taskSchedulerFallback);
    settings.setValue("paused", task.paused);
    settings.endGroup();
    settings.sync();
}

PersistedTask SettingsStore::loadTask() {
    PersistedTask task;
    QSettings settings;
    settings.beginGroup(kGroup);
    task.type = static_cast<PersistedTask::Type>(settings.value("type", 0).toInt());
    task.target = QDateTime::fromString(settings.value("target").toString(), Qt::ISODateWithMs);
    task.remainingSeconds = settings.value("remainingSeconds", 0).toLongLong();
    task.force = settings.value("force", false).toBool();
    task.taskSchedulerFallback = settings.value("taskSchedulerFallback", false).toBool();
    task.paused = settings.value("paused", false).toBool();
    settings.endGroup();
    if (task.type != PersistedTask::Type::ScheduledAt && task.type != PersistedTask::Type::Countdown) {
        task = {};
    }
    return task;
}

void SettingsStore::clearTask() {
    QSettings settings;
    settings.remove(kGroup);
    settings.sync();
}

bool SettingsStore::hasTask() {
    QSettings settings;
    return settings.contains(QStringLiteral("%1/type").arg(kGroup));
}
