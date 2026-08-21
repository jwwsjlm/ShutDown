#pragma once

#include "NativeTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef SHUTDOWN_USE_VELOPACK
#include "Velopack.hpp"
#endif

class UpdateManager {
public:
    struct Callbacks {
        std::function<void()> checkingStarted;
        std::function<void()> checkingFinished;
        std::function<void(const UpdateInfo &)> updateAvailable;
        std::function<void()> noUpdateAvailable;
        std::function<void(const std::wstring &)> checkError;
        std::function<void(std::int64_t, std::int64_t)> downloadProgress;
        std::function<void(const std::wstring &)> downloadFinished;
        std::function<void(const std::wstring &)> downloadError;
    };

    explicit UpdateManager(std::string currentVersion);
    ~UpdateManager();

    void setCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }
    void checkForUpdates();
    void downloadUpdate(const UpdateInfo &info);
    void cancelDownload();
    bool installAndRestart(const std::wstring &downloadedFile, std::wstring *errorMessage = nullptr) const;

    static bool parseRelease(const std::string &object, UpdateInfo *info);
    static bool parseReleasePage(const std::string &html, UpdateInfo *info);
    static bool parseDescriptor(const std::string &object, UpdateInfo *info);
    static std::string normalizeVersion(const std::string &value);
    static std::string currentArchitectureToken();

private:
    void joinWorker();
    bool isNewerThanCurrent(const std::string &version) const;
    std::string currentVersion() const { return m_currentVersion; }

    std::string m_currentVersion;
    Callbacks m_callbacks;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
#ifdef SHUTDOWN_USE_VELOPACK
    mutable std::mutex m_velopackMutex;
    std::optional<Velopack::UpdateInfo> m_velopackAvailable;
    std::optional<Velopack::UpdateInfo> m_velopackDownloaded;
#endif
};
