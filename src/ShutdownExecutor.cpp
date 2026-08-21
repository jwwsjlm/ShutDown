#include "ShutdownExecutor.h"

#include <QDateTime>
#include <QProcess>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#endif

namespace {
QString winError(const QString &operation) {
#ifdef Q_OS_WIN
    return QStringLiteral("%1 失败，Windows 错误码 %2").arg(operation).arg(GetLastError());
#else
    Q_UNUSED(operation);
    return QStringLiteral("当前平台不支持 Windows 关机");
#endif
}
}

bool ShutdownExecutor::execute(bool force, QString *errorMessage) {
#ifndef Q_OS_WIN
    if (errorMessage) *errorMessage = QStringLiteral("当前平台不支持 Windows 关机");
    return false;
#else
    QString error;
    enableShutdownPrivilege(&error);
    if (executeExitWindows(force, &error)) return true;
    if (executeInitiateSystemShutdown(force, &error)) return true;
    if (executeCommandLine(force, &error)) return true;
    if (errorMessage) *errorMessage = error;
    return false;
#endif
}

bool ShutdownExecutor::enableShutdownPrivilege(QString *errorMessage) {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        if (errorMessage) *errorMessage = winError(QStringLiteral("OpenProcessToken"));
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        if (errorMessage) *errorMessage = winError(QStringLiteral("LookupPrivilegeValue"));
        return false;
    }
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const BOOL ok = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    const DWORD lastError = GetLastError();
    CloseHandle(token);
    if (!ok || lastError == ERROR_NOT_ALL_ASSIGNED) {
        if (errorMessage) *errorMessage = winError(QStringLiteral("AdjustTokenPrivileges"));
        return false;
    }
    return true;
#else
    Q_UNUSED(errorMessage);
    return false;
#endif
}

bool ShutdownExecutor::executeExitWindows(bool force, QString *errorMessage) {
#ifdef Q_OS_WIN
    UINT flags = EWX_SHUTDOWN;
    if (force) flags |= EWX_FORCEIFHUNG;
    if (ExitWindowsEx(flags, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER)) return true;
    if (errorMessage) *errorMessage = winError(QStringLiteral("ExitWindowsEx"));
    return false;
#else
    Q_UNUSED(force); Q_UNUSED(errorMessage); return false;
#endif
}

bool ShutdownExecutor::executeInitiateSystemShutdown(bool force, QString *errorMessage) {
#ifdef Q_OS_WIN
    const BOOL ok = InitiateSystemShutdownExW(nullptr, nullptr, 0, force ? TRUE : FALSE,
                                               FALSE, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER);
    if (ok) return true;
    if (errorMessage) *errorMessage = winError(QStringLiteral("InitiateSystemShutdownEx"));
    return false;
#else
    Q_UNUSED(force); Q_UNUSED(errorMessage); return false;
#endif
}

bool ShutdownExecutor::executeCommandLine(bool force, QString *errorMessage) {
#ifdef Q_OS_WIN
    QStringList args{QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0")};
    if (force) args << QStringLiteral("/f");
    if (QProcess::startDetached(QStringLiteral("shutdown.exe"), args)) return true;
    if (errorMessage) *errorMessage = QStringLiteral("无法启动 shutdown.exe");
    return false;
#else
    Q_UNUSED(force); Q_UNUSED(errorMessage); return false;
#endif
}
