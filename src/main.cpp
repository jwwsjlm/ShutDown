#include "MainWindow.h"
#include "AppLogger.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#if defined(QT_STATIC)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifndef SHUTDOWN_VERSION
#define SHUTDOWN_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Prefer a Windows font with complete Simplified Chinese glyph coverage.
    // Qt still falls back automatically when the font is unavailable.
    QFont uiFont = app.font();
    uiFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    app.setFont(uiFont);
#endif
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
