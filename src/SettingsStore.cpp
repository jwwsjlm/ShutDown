#include "SettingsStore.h"

#include <windows.h>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
constexpr std::int64_t kMaxSettingsBytes = 64 * 1024;

class FileHandle {
public:
    explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
    ~FileHandle() { if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle); }
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;
    HANDLE get() const { return m_handle; }
    bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle;
};

std::wstring settingsDirectory() {
    const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    std::wstring root;
    if (required > 1) {
        root.resize(required);
        const DWORD written = GetEnvironmentVariableW(L"APPDATA", root.data(), required);
        if (written > 0 && written < required) root.resize(written);
        else root.clear();
    }
    if (root.empty()) {
        wchar_t temporary[MAX_PATH]{};
        const DWORD written = GetTempPathW(MAX_PATH, temporary);
        if (written > 0 && written < MAX_PATH) root.assign(temporary, written);
    }
    if (!root.empty() && root.back() != L'\\' && root.back() != L'/') root.push_back(L'\\');
    root += L"ShutDown";
    return root;
}

std::wstring settingsPath() { return settingsDirectory() + L"\\settings.ini"; }

std::string readSettings() {
    FileHandle file(CreateFileW(settingsPath().c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) return {};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0 || size.QuadPart > kMaxSettingsBytes) return {};

    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &bytesRead, nullptr)) return {};
    content.resize(bytesRead);
    return content;
}

bool findValue(std::string_view content, std::string_view key, std::string_view &value) {
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto lineEnd = content.find('\n', offset);
        const auto length = (lineEnd == std::string_view::npos ? content.size() : lineEnd) - offset;
        auto line = content.substr(offset, length);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=') {
            value = line.substr(key.size() + 1);
            return true;
        }
        if (lineEnd == std::string_view::npos) break;
        offset = lineEnd + 1;
    }
    return false;
}

template <typename Integer>
bool readInteger(std::string_view content, std::string_view key, Integer &result) {
    std::string_view value;
    if (!findValue(content, key, value) || value.empty()) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

bool readBoolean(std::string_view content, std::string_view key) {
    std::string_view value;
    return findValue(content, key, value) && value == "1";
}

template <typename Integer>
void appendInteger(std::string &content, std::string_view key, Integer value) {
    char buffer[32]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (converted.ec != std::errc{}) return;
    content.append(key.data(), key.size());
    content.push_back('=');
    content.append(buffer, converted.ptr);
    content.push_back('\n');
}

void appendBoolean(std::string &content, std::string_view key, bool value) {
    content.append(key.data(), key.size());
    content += value ? "=1\n" : "=0\n";
}

bool writeSettings(const std::string &content) {
    const std::wstring directory = settingsDirectory();
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;

    const std::wstring destination = directory + L"\\settings.ini";
    const std::wstring temporary = destination + L".tmp";
    bool success = false;
    {
        FileHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.valid()) return false;
        DWORD bytesWritten = 0;
        success = WriteFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &bytesWritten, nullptr) &&
                  bytesWritten == content.size();
    }
    if (success) {
        success = MoveFileExW(temporary.c_str(), destination.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == TRUE;
    }
    if (!success) DeleteFileW(temporary.c_str());
    return success;
}
}

void SettingsStore::saveTask(const PersistedTask &task) {
    std::string content;
    content.reserve(128);
    appendInteger(content, "type", static_cast<int>(task.type));
    appendInteger(content, "targetEpoch", task.targetEpoch);
    appendInteger(content, "remainingSeconds", task.remainingSeconds);
    appendBoolean(content, "force", task.force);
    appendBoolean(content, "taskSchedulerFallback", task.taskSchedulerFallback);
    appendBoolean(content, "paused", task.paused);
    writeSettings(content);
}

PersistedTask SettingsStore::loadTask() {
    const std::string content = readSettings();
    if (content.empty()) return {};

    PersistedTask task;
    int type = 0;
    if (!readInteger(content, "type", type) ||
        !readInteger(content, "targetEpoch", task.targetEpoch) ||
        !readInteger(content, "remainingSeconds", task.remainingSeconds)) return {};
    task.type = static_cast<PersistedTask::Type>(type);
    task.force = readBoolean(content, "force");
    task.taskSchedulerFallback = readBoolean(content, "taskSchedulerFallback");
    task.paused = readBoolean(content, "paused");
    if (task.type != PersistedTask::Type::ScheduledAt && task.type != PersistedTask::Type::Countdown) return {};
    return task;
}

void SettingsStore::clearTask() { DeleteFileW(settingsPath().c_str()); }

bool SettingsStore::hasTask() { return loadTask().type != PersistedTask::Type::None; }
