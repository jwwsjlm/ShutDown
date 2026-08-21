#include "MainWindow.h"
#include "AppLogger.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#ifndef SHUTDOWN_VERSION
#define SHUTDOWN_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ShutDown"));
    QApplication::setApplicationVersion(QStringLiteral(SHUTDOWN_VERSION));
    QApplication::setOrganizationName(QStringLiteral("ShutDown"));
    AppLogger::initialize();

    const QString lockPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/ShutDown.lock");
    QLockFile lock(lockPath);
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, QStringLiteral("定时关机"), QStringLiteral("程序已经在运行。"));
        return 0;
    }

    MainWindow window;
    window.show();
    return app.exec();
}
