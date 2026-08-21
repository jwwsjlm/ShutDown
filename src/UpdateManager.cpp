#include "UpdateManager.h"

#include <windows.h>

#include <array>
#include <exception>
#include <memory>
#include <sstream>
#include <utility>

namespace {

std::wstring utf8ToWide(const std::string &value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

#ifdef SHUTDOWN_USE_VELOPACK
std::unique_ptr<Velopack::UpdateManager> createVelopackManager() {
    auto source = std::make_unique<Velopack::GithubSource>("https://github.com/jwwsjlm/ShutDown");
    return std::make_unique<Velopack::UpdateManager>(std::move(source));
}

UpdateInfo toNativeUpdateInfo(const Velopack::UpdateInfo &update) {
    UpdateInfo info;
    info.version = UpdateManager::normalizeVersion(update.TargetFullRelease.Version);
    info.tagName = "v" + info.version;
    info.title = "Velopack Release";
    info.notes = update.TargetFullRelease.NotesMarkdown;
    info.assetName = update.TargetFullRelease.FileName;
    info.downloadUrl = "velopack://github/jwwsjlm/ShutDown/" + update.TargetFullRelease.FileName;
    return info;
}

void velopackProgress(void *userData, size_t progress) {
    auto *callbacks = reinterpret_cast<UpdateManager::Callbacks *>(userData);
    if (callbacks && callbacks->downloadProgress) {
        callbacks->downloadProgress(static_cast<std::int64_t>(progress), 100);
    }
}
#endif

} // namespace

UpdateManager::UpdateManager(std::string currentVersion) : m_currentVersion(std::move(currentVersion)) {}
UpdateManager::~UpdateManager() { cancelDownload(); joinWorker(); }

void UpdateManager::joinWorker() {
    if (m_worker.joinable()) m_worker.join();
}

std::string UpdateManager::currentArchitectureToken() {
#ifdef _WIN64
    return "x64";
#else
    return "x86";
#endif
}

std::string UpdateManager::normalizeVersion(const std::string &value) {
    std::string result = value;
    while (!result.empty() && (result[0] == 'v' || result[0] == 'V')) result.erase(result.begin());
    const auto pos = result.find_first_of("-+");
    if (pos != std::string::npos) result.resize(pos);
    return result;
}

bool UpdateManager::isNewerThanCurrent(const std::string &version) const {
    auto parse = [](const std::string &s) {
        std::array<int, 3> out{};
        std::stringstream ss(s);
        char dot;
        ss >> out[0] >> dot >> out[1] >> dot >> out[2];
        return out;
    };
    return parse(normalizeVersion(version)) > parse(normalizeVersion(m_currentVersion));
}

void UpdateManager::checkForUpdates() {
    joinWorker();
    m_cancel = false;
    if (m_callbacks.checkingStarted) m_callbacks.checkingStarted();
    m_worker = std::thread([this] {
#ifdef SHUTDOWN_USE_VELOPACK
        try {
            auto manager = createVelopackManager();
            if (manager->IsPortable()) {
                if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
                if (m_callbacks.checkError) {
                    m_callbacks.checkError(L"当前是便携版或未通过 Velopack 安装，自动更新仅支持 Setup.exe 安装版。请从 GitHub Release 下载最新版安装包。");
                }
                return;
            }

            const auto update = manager->CheckForUpdates();
            if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
            if (update.has_value()) {
                {
                    std::lock_guard<std::mutex> lock(m_velopackMutex);
                    m_velopackAvailable = update;
                    m_velopackDownloaded.reset();
                }
                if (m_callbacks.updateAvailable) m_callbacks.updateAvailable(toNativeUpdateInfo(update.value()));
            } else if (m_callbacks.noUpdateAvailable) {
                m_callbacks.noUpdateAvailable();
            }
        } catch (const std::exception &ex) {
            if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
            if (m_callbacks.checkError) m_callbacks.checkError(utf8ToWide(ex.what()));
        } catch (...) {
            if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
            if (m_callbacks.checkError) m_callbacks.checkError(L"Velopack 检查更新失败");
        }
#else
        if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
        if (m_callbacks.checkError) {
            m_callbacks.checkError(L"当前构建未启用 Velopack，无法自动更新。请使用 GitHub Release 中的 Setup.exe 安装版。");
        }
#endif
    });
}

void UpdateManager::downloadUpdate(const UpdateInfo &info) {
    joinWorker();
    m_cancel = false;
    m_worker = std::thread([this, info] {
#ifdef SHUTDOWN_USE_VELOPACK
        std::optional<Velopack::UpdateInfo> velopackUpdate;
        {
            std::lock_guard<std::mutex> lock(m_velopackMutex);
            velopackUpdate = m_velopackAvailable;
        }
        if (!velopackUpdate.has_value() || info.downloadUrl.rfind("velopack://", 0) != 0) {
            if (m_callbacks.downloadError) m_callbacks.downloadError(L"无效的 Velopack 更新信息");
            return;
        }

        try {
            auto manager = createVelopackManager();
            Callbacks callbacks = m_callbacks;
            manager->DownloadUpdates(velopackUpdate.value(), velopackProgress, &callbacks);
            {
                std::lock_guard<std::mutex> lock(m_velopackMutex);
                m_velopackDownloaded = velopackUpdate;
            }
            if (m_callbacks.downloadFinished) m_callbacks.downloadFinished(L"velopack");
        } catch (const std::exception &ex) {
            if (m_callbacks.downloadError) m_callbacks.downloadError(utf8ToWide(ex.what()));
        } catch (...) {
            if (m_callbacks.downloadError) m_callbacks.downloadError(L"Velopack 下载更新失败");
        }
#else
        (void)info;
        if (m_callbacks.downloadError) m_callbacks.downloadError(L"当前构建未启用 Velopack，无法下载更新");
#endif
    });
}

void UpdateManager::cancelDownload() { m_cancel = true; }

bool UpdateManager::installAndRestart(const std::wstring &downloadedFile, std::wstring *errorMessage) const {
#ifdef SHUTDOWN_USE_VELOPACK
    if (downloadedFile != L"velopack") {
        if (errorMessage) *errorMessage = L"无效的 Velopack 更新状态";
        return false;
    }

    try {
        std::optional<Velopack::UpdateInfo> update;
        {
            std::lock_guard<std::mutex> lock(m_velopackMutex);
            update = m_velopackDownloaded.has_value() ? m_velopackDownloaded : m_velopackAvailable;
        }
        if (!update.has_value()) {
            if (errorMessage) *errorMessage = L"没有可安装的 Velopack 更新";
            return false;
        }
        auto manager = createVelopackManager();
        manager->WaitExitThenApplyUpdates(update.value(), false, true);
        return true;
    } catch (const std::exception &ex) {
        if (errorMessage) *errorMessage = utf8ToWide(ex.what());
        return false;
    } catch (...) {
        if (errorMessage) *errorMessage = L"Velopack 启动更新器失败";
        return false;
    }
#else
    (void)downloadedFile;
    if (errorMessage) *errorMessage = L"当前构建未启用 Velopack，无法安装更新";
    return false;
#endif
}