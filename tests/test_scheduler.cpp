#include "SettingsStore.h"

#include <QCoreApplication>
#include <QTest>

class SettingsStoreTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTrip() {
        PersistedTask original;
        original.type = PersistedTask::Type::ScheduledAt;
        original.target = QDateTime::currentDateTime().addSecs(3600);
        original.remainingSeconds = 3600;
        original.force = true;
        original.taskSchedulerFallback = true;
        original.paused = true;
        SettingsStore::saveTask(original);
        const auto loaded = SettingsStore::loadTask();
        QCOMPARE(static_cast<int>(loaded.type), static_cast<int>(original.type));
        QCOMPARE(loaded.target.toString(Qt::ISODateWithMs), original.target.toString(Qt::ISODateWithMs));
        QCOMPARE(loaded.remainingSeconds, original.remainingSeconds);
        QCOMPARE(loaded.force, original.force);
        QCOMPARE(loaded.taskSchedulerFallback, original.taskSchedulerFallback);
        QCOMPARE(loaded.paused, original.paused);
        SettingsStore::clearTask();
        QVERIFY(!SettingsStore::hasTask());
    }
};

QTEST_MAIN(SettingsStoreTest)
#include "test_scheduler.moc"
