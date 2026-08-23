#include "UpdateManager.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <array>
#include <charconv>

namespace {

constexpr wchar_t kGitHubHost[] = L"api.github.com";
constexpr wchar_t kLatestReleasePath[] = L"/repos/jwwsjlm/ShutDown/releases/latest";
constexpr wchar_t kJsDelivrDataHost[] = L"data.jsdelivr.com";
constexpr wchar_t kJsDelivrDataPath[] = L"/v1/package/gh/jwwsjlm/ShutDown";
constexpr wchar_t kJsDelivrVersionPath[] = L"/gh/jwwsjlm/ShutDown@latest/CMakeLists.txt";
constexpr const wchar_t *kJsDelivrHosts[] = {
    L"cdn.jsdelivr.net",
    L"fastly.jsdelivr.net",
    L"testingcf.jsdelivr.net",
    L"gcore.jsdelivr.net",
};

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : m_handle(handle) {}
    ~InternetHandle() { if (m_handle) WinHttpCloseHandle(m_handle); }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    HINTERNET get() const { return m_handle; }

private:
    HINTERNET m_handle;
};

std::wstring requestError(const wchar_t *action) {
    return std::wstring(action) + L"（错误码 " + std::to_wstring(GetLastError()) + L"）";
}

bool parseVersion(const std::string &value, std::array<int, 3> &parts) {
    const std::string normalized = UpdateManager::normalizeVersion(value);
    const char *current = normalized.data();
    const char *const end = current + normalized.size();
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto parsed = std::from_chars(current, end, parts[index]);
        if (parsed.ec != std::errc{} || parsed.ptr == current || parts[index] < 0) return false;
        current = parsed.ptr;
        if (index + 1 < parts.size()) {
            if (current == end || *current != '.') return false;
            ++current;
        }
    }
    return current == end;
}

bool readJsonString(const std::string &json, const std::string &name, std::string &value) {
    const std::string key = "\"" + name + "\"";
    auto position = json.find(key);
    if (position == std::string::npos) return false;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return false;
    position = json.find('"', position + 1);
    if (position == std::string::npos) return false;

    value.clear();
    for (++position; position < json.size(); ++position) {
        const char current = json[position];
        if (current == '"') return true;
        if (current == '\\' && position + 1 < json.size()) {
            value.push_back(json[++position]);
        } else {
            value.push_back(current);
        }
    }
    return false;
}

bool readJsonArrayFirstString(const std::string &json, const std::string &name, std::string &value) {
    const std::string key = "\"" + name + "\"";
    auto position = json.find(key);
    if (position == std::string::npos) return false;
    position = json.find('[', position + key.size());
    if (position == std::string::npos) return false;
    position = json.find('"', position + 1);
    if (position == std::string::npos) return false;

    value.clear();
    for (++position; position < json.size(); ++position) {
        const char current = json[position];
        if (current == '"') return true;
        if (current == '\\' && position + 1 < json.size()) {
            value.push_back(json[++position]);
        } else {
            value.push_back(current);
        }
    }
    return false;
}

bool fetchJson(const std::atomic<bool> &cancelRequested, HINTERNET session,
               const wchar_t *host, const wchar_t *path,
               std::string &body, std::wstring &error) {
    if (cancelRequested.load(std::memory_order_relaxed)) {
        error = L"更新检查已取消";
        return false;
    }
    InternetHandle connection(WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.get()) {
        error = requestError(L"无法连接更新服务器");
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path,
                                               nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE));
    if (!request.get()) {
        error = requestError(L"无法创建更新请求");
        return false;
    }

    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        error = requestError(L"更新检查失败");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        error = requestError(L"无法读取更新响应");
        return false;
    }
    if (status == 403 || status == 429) {
        error = L"更新服务器拒绝了请求（HTTP " + std::to_wstring(status) + L"）";
        return false;
    }
    if (status != 200) {
        error = L"更新服务器返回错误（HTTP " + std::to_wstring(status) + L"）";
        return false;
    }

    body.clear();
    std::array<char, 4096> buffer{};
    for (;;) {
        if (cancelRequested.load(std::memory_order_relaxed)) {
            error = L"更新检查已取消";
            return false;
        }
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
            error = requestError(L"无法读取更新响应");
            return false;
        }
        if (bytesRead == 0) break;
        body.append(buffer.data(), bytesRead);
        if (body.size() > 1024 * 1024) {
            error = L"更新服务器返回的信息过大";
            return false;
        }
    }

    return true;
}

bool parseLatestVersion(const std::string &body, const char *field, std::string &version, std::wstring &error) {
    if (!readJsonString(body, field, version)) {
        error = L"更新信息格式无效";
        return false;
    }
    version = UpdateManager::normalizeVersion(version);
    std::array<int, 3> parsed{};
    if (!parseVersion(version, parsed)) {
        error = L"更新信息中的版本号无效";
        return false;
    }
    return true;
}

bool parseCMakeVersion(const std::string &body, std::string &version, std::wstring &error) {
    const auto keyPosition = body.find("SHUTDOWN_BASE_VERSION");
    if (keyPosition == std::string::npos) {
        error = L"更新信息格式无效";
        return false;
    }
    const auto valuePosition = body.find_first_of("0123456789", keyPosition);
    if (valuePosition == std::string::npos) {
        error = L"更新信息中的版本号无效";
        return false;
    }
    auto endPosition = valuePosition;
    while (endPosition < body.size() &&
           (body[endPosition] == '.' || (body[endPosition] >= '0' && body[endPosition] <= '9'))) {
        ++endPosition;
    }
    version = UpdateManager::normalizeVersion(body.substr(valuePosition, endPosition - valuePosition));
    std::array<int, 3> parsed{};
    if (!parseVersion(version, parsed)) {
        error = L"更新信息中的版本号无效";
        return false;
    }
    return true;
}

bool fetchLatestVersion(const std::atomic<bool> &cancelRequested,
                        std::string &version, std::wstring &error) {
    std::wstring lastError = L"无法连接更新服务器";
    InternetHandle session(WinHttpOpen(L"ShutDown update checker",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS,
                                       0));
    if (!session.get()) {
        error = requestError(L"无法初始化网络请求");
        return false;
    }
    WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000);

    // jsDelivr 的 package 元数据直接对应 GitHub 仓库的 tags/releases，优先使用它。
    std::string body;
    if (fetchJson(cancelRequested, session.get(), kJsDelivrDataHost, kJsDelivrDataPath, body, lastError) &&
        readJsonArrayFirstString(body, "versions", version)) {
        version = UpdateManager::normalizeVersion(version);
        std::array<int, 3> parsed{};
        if (parseVersion(version, parsed)) return true;
        lastError = L"更新信息中的版本号无效";
    }

    // 元数据节点不可用时，尝试多个 jsDelivr CDN 节点读取仓库版本定义。
    for (const auto *host : kJsDelivrHosts) {
        if (cancelRequested.load(std::memory_order_relaxed)) return false;
        body.clear();
        if (fetchJson(cancelRequested, session.get(), host, kJsDelivrVersionPath, body, lastError) &&
            parseCMakeVersion(body, version, lastError)) {
            return true;
        }
    }

    // 最后回退 GitHub Releases API，仍然读取 releases/latest，不下载任何文件。
    if (cancelRequested.load(std::memory_order_relaxed)) return false;
    body.clear();
    if (fetchJson(cancelRequested, session.get(), kGitHubHost, kLatestReleasePath, body, lastError) &&
        parseLatestVersion(body, "tag_name", version, lastError)) {
        return true;
    }

    error = lastError;
    return false;
}

} // namespace

UpdateManager::UpdateManager(std::string currentVersion) : m_currentVersion(std::move(currentVersion)) {}
UpdateManager::~UpdateManager() { stopAndJoin(); }

void UpdateManager::stopAndJoin() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    joinWorker();
}

void UpdateManager::joinWorker() {
    if (m_worker.joinable()) m_worker.join();
}

std::string UpdateManager::normalizeVersion(const std::string &value) {
    std::size_t start = 0;
    while (start < value.size() && (value[start] == 'v' || value[start] == 'V')) ++start;
    const auto end = value.find_first_of("-+", start);
    return value.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool UpdateManager::isNewerVersion(const std::string &candidate, const std::string &current) {
    std::array<int, 3> candidateParts{};
    std::array<int, 3> currentParts{};
    return parseVersion(candidate, candidateParts) && parseVersion(current, currentParts) &&
           candidateParts > currentParts;
}

void UpdateManager::checkForUpdates() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    joinWorker();
    m_cancelRequested.store(false, std::memory_order_relaxed);
    const auto currentVersion = m_currentVersion;
    const auto callbacks = m_callbacks;
    const auto *cancelRequested = &m_cancelRequested;
    m_worker = std::thread([currentVersion, callbacks, cancelRequested] {
        std::string latestVersion;
        std::wstring error;
        if (!fetchLatestVersion(*cancelRequested, latestVersion, error)) {
            if (!cancelRequested->load(std::memory_order_relaxed) && callbacks.checkError) callbacks.checkError(error);
            return;
        }
        if (cancelRequested->load(std::memory_order_relaxed)) return;
        if (UpdateManager::isNewerVersion(latestVersion, currentVersion)) {
            if (callbacks.updateAvailable) callbacks.updateAvailable(latestVersion);
        } else if (callbacks.noUpdateAvailable) {
            callbacks.noUpdateAvailable();
        }
    });
}
