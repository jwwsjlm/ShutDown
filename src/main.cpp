#include "AppLogger.h"
#include "MainWindow.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#ifndef SHUTDOWN_VERSION
#define SHUTDOWN_VERSION "0.0.0-dev"
#endif

class ShutdownApp final : public Win32xx::CWinApp {};

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    ShutdownApp app;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    AppLogger::initialize();
    const auto mutex = CreateMutexW(nullptr, TRUE, L"Local\\ShutDown.SingleInstance");
    if (!mutex) {
        MessageBoxW(nullptr, L"无法创建程序互斥锁。", L"定时关机", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"程序已经在运行。", L"定时关机", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }
    MainWindow window(SHUTDOWN_VERSION);
    app.SetMainWnd(window.CreateMain());
    const int result = app.Run();
    CloseHandle(mutex);
    return result;
}
