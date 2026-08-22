#pragma once

#include <functional>
#include <string>
#include <thread>
#include <utility>

class UpdateManager {
public:
    struct Callbacks {
        std::function<void(const std::string &)> updateAvailable;
        std::function<void()> noUpdateAvailable;
        std::function<void(const std::wstring &)> checkError;
    };

    explicit UpdateManager(std::string currentVersion);
    ~UpdateManager();

    void setCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }
    void checkForUpdates();
    void stopAndJoin();

    static std::string normalizeVersion(const std::string &value);
    static bool isNewerVersion(const std::string &candidate, const std::string &current);

private:
    void joinWorker();

    std::string m_currentVersion;
    Callbacks m_callbacks;
    std::thread m_worker;
};
