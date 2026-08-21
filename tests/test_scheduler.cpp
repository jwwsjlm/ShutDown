#include "SettingsStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QCoreApplication::setOrganizationName(QStringLiteral("ShutDown"));
    QCoreApplication::setApplicationName(QStringLiteral("ShutDownTests"));

    int failures = 0;
    const auto fail = [&failures](const char *message) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    };

    SettingsStore::clearTask();
    PersistedTask original;
    original.type = PersistedTask::Type::ScheduledAt;
    original.target = QDateTime::currentDateTime().addSecs(3600);
    original.remainingSeconds = 3600;
    original.force = true;
    original.taskSchedulerFallback = true;
    original.paused = true;

    SettingsStore::saveTask(original);
    const auto loaded = SettingsStore::loadTask();
    if (loaded.type != original.type) fail("task type round-trip");
    if (loaded.target.toMSecsSinceEpoch() != original.target.toMSecsSinceEpoch()) fail("target round-trip");
    if (loaded.remainingSeconds != original.remainingSeconds) fail("remaining seconds round-trip");
    if (loaded.force != original.force) fail("force flag round-trip");
    if (loaded.taskSchedulerFallback != original.taskSchedulerFallback) fail("fallback flag round-trip");
    if (loaded.paused != original.paused) fail("paused flag round-trip");

    SettingsStore::clearTask();
    if (SettingsStore::hasTask()) fail("clear task");

    if (failures == 0) {
        std::fprintf(stdout, "SettingsStore round-trip: PASS\n");
        return 0;
    }
    return 1;
}
