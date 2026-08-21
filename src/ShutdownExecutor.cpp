#include "ShutdownExecutor.h"

#include <windows.h>
#include <string>

namespace {
std::wstring winError(const wchar_t *operation) {
    return std::wstring(operation) + L" 失败，Windows 错误码 " + std::to_wstring(GetLastError());
}
}

bool ShutdownExecutor::execute(bool force, std::wstring *errorMessage) {
    HANDLE token = nullptr;
    TOKEN_PRIVILEGES privileges{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }

    UINT flags = EWX_SHUTDOWN;
    if (force) flags |= EWX_FORCEIFHUNG;
    if (ExitWindowsEx(flags, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER)) return true;
    const DWORD firstError = GetLastError();

    if (InitiateSystemShutdownExW(nullptr, nullptr, 0, force ? TRUE : FALSE, FALSE,
                                  SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER)) return true;
    if (errorMessage) *errorMessage = winError(L"ExitWindowsEx/InitiateSystemShutdownEx");
    SetLastError(firstError);
    return false;
}
