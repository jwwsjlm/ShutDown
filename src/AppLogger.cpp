#include "AppLogger.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>

namespace {
std::mutex g_mutex;
std::wofstream g_file;

std::filesystem::path logPath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    std::filesystem::path root = length ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path();
    root /= L"ShutDown";
    root /= L"logs";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root / L"shutdown.log";
}
}

void AppLogger::initialize() {
    g_file.open(logPath(), std::ios::app);
}

void AppLogger::write(const std::wstring &level, const std::wstring &message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file.is_open()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    g_file << L"[" << st.wYear << L"-" << std::setfill(L'0') << std::setw(2) << st.wMonth
           << L"-" << std::setw(2) << st.wDay << L" " << std::setw(2) << st.wHour
           << L":" << std::setw(2) << st.wMinute << L":" << std::setw(2) << st.wSecond
           << L"] [" << level << L"] " << message << std::endl;
}
