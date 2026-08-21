#include "SettingsStore.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {
std::filesystem::path settingsPath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    std::filesystem::path root = length ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path();
    root /= L"ShutDown";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root / L"settings.ini";
}

std::map<std::string, std::string> readValues() {
    std::map<std::string, std::string> values;
    std::ifstream file(settingsPath(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        const auto pos = line.find('=');
        if (pos != std::string::npos) values[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return values;
}

void writeValues(const std::map<std::string, std::string> &values) {
    std::ofstream file(settingsPath(), std::ios::binary | std::ios::trunc);
    for (const auto &entry : values) file << entry.first << '=' << entry.second << '\n';
}

bool boolValue(const std::map<std::string, std::string> &v, const char *key) {
    const auto it = v.find(key);
    return it != v.end() && it->second == "1";
}
}

void SettingsStore::saveTask(const PersistedTask &task) {
    auto values = readValues();
    values["type"] = std::to_string(static_cast<int>(task.type));
    values["targetEpoch"] = std::to_string(task.targetEpoch);
    values["remainingSeconds"] = std::to_string(task.remainingSeconds);
    values["force"] = task.force ? "1" : "0";
    values["taskSchedulerFallback"] = task.taskSchedulerFallback ? "1" : "0";
    values["paused"] = task.paused ? "1" : "0";
    writeValues(values);
}

PersistedTask SettingsStore::loadTask() {
    PersistedTask task;
    const auto values = readValues();
    try {
        task.type = static_cast<PersistedTask::Type>(std::stoi(values.at("type")));
        task.targetEpoch = std::stoll(values.at("targetEpoch"));
        task.remainingSeconds = std::stoll(values.at("remainingSeconds"));
    } catch (...) {
        return {};
    }
    task.force = boolValue(values, "force");
    task.taskSchedulerFallback = boolValue(values, "taskSchedulerFallback");
    task.paused = boolValue(values, "paused");
    if (task.type != PersistedTask::Type::ScheduledAt && task.type != PersistedTask::Type::Countdown) return {};
    return task;
}

void SettingsStore::clearTask() {
    std::error_code ec;
    std::filesystem::remove(settingsPath(), ec);
}

bool SettingsStore::hasTask() {
    const auto values = readValues();
    return values.find("type") != values.end();
}
