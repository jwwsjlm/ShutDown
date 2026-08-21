#include "TaskSchedulerFallback.h"

#include <QProcess>
#include <QDebug>
#include <QStringList>

QString TaskSchedulerFallback::taskName() {
    return QStringLiteral("ShutDown_OneShot");
}

bool TaskSchedulerFallback::create(const QDateTime &when, bool force, QString *errorMessage) {
#ifdef Q_OS_WIN
    remove(nullptr);
    const QString date = when.date().toString(QStringLiteral("MM/dd/yyyy"));
    const QString time = when.time().toString(QStringLiteral("HH:mm"));
    QString command = QStringLiteral("shutdown.exe /s /t 0");
    if (force) command += QStringLiteral(" /f");
    const QStringList args{
        QStringLiteral("/Create"), QStringLiteral("/TN"), taskName(),
        QStringLiteral("/TR"), command, QStringLiteral("/SC"), QStringLiteral("ONCE"),
        QStringLiteral("/SD"), date, QStringLiteral("/ST"), time,
        QStringLiteral("/RL"), QStringLiteral("HIGHEST"), QStringLiteral("/F")};
    QProcess process;
    process.start(QStringLiteral("schtasks.exe"), args);
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        if (errorMessage) *errorMessage = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("创建系统任务失败");
        qWarning() << "Task Scheduler create failed:" << (errorMessage ? *errorMessage : QString());
        return false;
    }
    qInfo() << "Task Scheduler fallback created for" << when;
    return true;
#else
    Q_UNUSED(when); Q_UNUSED(force);
    if (errorMessage) *errorMessage = QStringLiteral("当前平台不支持 Task Scheduler");
    return false;
#endif
}

bool TaskSchedulerFallback::remove(QString *errorMessage) {
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("schtasks.exe"), {QStringLiteral("/Delete"), QStringLiteral("/TN"), taskName(), QStringLiteral("/F")});
    if (!process.waitForFinished(5000)) {
        if (errorMessage) *errorMessage = QStringLiteral("删除系统任务超时");
        return false;
    }
    qInfo() << "Task Scheduler fallback removed";
    return process.exitCode() == 0 || process.exitCode() == 1;
#else
    Q_UNUSED(errorMessage); return false;
#endif
}

bool TaskSchedulerFallback::exists() {
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("schtasks.exe"), {QStringLiteral("/Query"), QStringLiteral("/TN"), taskName()});
    if (!process.waitForFinished(3000)) return false;
    return process.exitCode() == 0;
#else
    return false;
#endif
}
