#include "AppLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace {
QFile g_logFile;
QMutex g_logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    Q_UNUSED(context);
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile.isOpen()) return;
    const char *level = "INFO";
    if (type == QtWarningMsg) level = "WARN";
    else if (type == QtCriticalMsg) level = "ERROR";
    else if (type == QtFatalMsg) level = "FATAL";
    QTextStream stream(&g_logFile);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " [" << level << "] " << message << Qt::endl;
}
}

void AppLogger::initialize() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(directory);
    g_logFile.setFileName(directory + QStringLiteral("/shutdown.log"));
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
    qInfo() << "Application logger initialized at" << g_logFile.fileName();
}
