#pragma once

#include <QString>

class ShutdownExecutor {
public:
    static bool execute(bool force, QString *errorMessage = nullptr);

private:
    static bool enableShutdownPrivilege(QString *errorMessage);
    static bool executeExitWindows(bool force, QString *errorMessage);
    static bool executeInitiateSystemShutdown(bool force, QString *errorMessage);
    static bool executeCommandLine(bool force, QString *errorMessage);
};
