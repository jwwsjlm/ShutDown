#include "TaskSchedulerFallback.h"

#include <windows.h>
#include <array>
#include <cstdio>
#include <sstream>

namespace {
std::wstring run(const std::wstring &command, DWORD *exitCode = nullptr) {
    std::array<wchar_t, 4096> buffer{};
    std::wstring cmd = command;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) return L"CreateProcess failed";
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exitCode) *exitCode = code;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0 ? L"" : L"系统任务命令执行失败";
}
}

std::wstring TaskSchedulerFallback::taskName() { return L"ShutDown_OneShot"; }

bool TaskSchedulerFallback::create(std::time_t when, bool force, std::wstring *errorMessage) {
    remove(nullptr);
    std::tm local{};
    localtime_s(&local, &when);
    wchar_t date[32]{}, time[32]{};
    std::swprintf(date, 32, L"%02d/%02d/%04d", local.tm_mon + 1, local.tm_mday, local.tm_year + 1900);
    std::swprintf(time, 32, L"%02d:%02d", local.tm_hour, local.tm_min);
    std::wstring command = L"schtasks.exe /Create /TN \"" + taskName() + L"\" /TR \"shutdown.exe /s /t 0";
    if (force) command += L" /f";
    command += L"\" /SC ONCE /SD " + std::wstring(date) + L" /ST " + std::wstring(time) + L" /RL HIGHEST /F";
    DWORD code = 1;
    const auto error = run(command, &code);
    if (code != 0) {
        if (errorMessage) *errorMessage = error.empty() ? L"创建系统任务失败" : error;
        return false;
    }
    return true;
}

bool TaskSchedulerFallback::remove(std::wstring *errorMessage) {
    DWORD code = 1;
    const auto error = run(L"schtasks.exe /Delete /TN \"" + taskName() + L"\" /F", &code);
    if (code != 0 && code != 1 && errorMessage) *errorMessage = error.empty() ? L"删除系统任务失败" : error;
    return code == 0 || code == 1;
}

bool TaskSchedulerFallback::exists() {
    DWORD code = 1;
    run(L"schtasks.exe /Query /TN \"" + taskName() + L"\"", &code);
    return code == 0;
}
