#include "UpdateManager.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <sstream>

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
    std::istringstream stream(normalized);
    char firstDot = 0;
    char secondDot = 0;
    if (!(stream >> parts[0] >> firstDot >> parts[1] >> secondDot >> parts[2])) return false;
    stream >> std::ws;
    return firstDot == '.' && secondDot == '.' && stream.eof() &&
           parts[0] >= 0 && parts[1] >= 0 && parts[2] >= 0;
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

bool fetchJson(const wchar_t *host, const wchar_t *path, std::string &body, std::wstring &error) {
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

    InternetHandle connection(WinHttpConnect(session.get(), host, INTERNET_DEFAULT_HTTPS_PORT, 0));
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

bool fetchLatestVersion(std::string &version, std::wstring &error) {
    std::wstring lastError = L"无法连接更新服务器";

    // jsDelivr 的 package 元数据直接对应 GitHub 仓库的 tags/releases，优先使用它。
    std::string body;
    if (fetchJson(kJsDelivrDataHost, kJsDelivrDataPath, body, lastError) &&
        readJsonArrayFirstString(body, "versions", version)) {
        version = UpdateManager::normalizeVersion(version);
        std::array<int, 3> parsed{};
        if (parseVersion(version, parsed)) return true;
        lastError = L"更新信息中的版本号无效";
    }

    // 元数据节点不可用时，尝试多个 jsDelivr CDN 节点读取仓库版本定义。
    for (const auto *host : kJsDelivrHosts) {
        body.clear();
        if (fetchJson(host, kJsDelivrVersionPath, body, lastError) &&
            parseCMakeVersion(body, version, lastError)) {
            return true;
        }
    }

    // 最后回退 GitHub Releases API，仍然读取 releases/latest，不下载任何文件。
    body.clear();
    if (fetchJson(kGitHubHost, kLatestReleasePath, body, lastError) &&
        parseLatestVersion(body, "tag_name", version, lastError)) {
        return true;
    }

    error = lastError;
    return false;
}

} // namespace

UpdateManager::UpdateManager(std::string currentVersion) : m_currentVersion(std::move(currentVersion)) {}
UpdateManager::~UpdateManager() { stopAndJoin(); }

void UpdateManager::stopAndJoin() { joinWorker(); }

void UpdateManager::joinWorker() {
    if (m_worker.joinable()) m_worker.join();
}

std::string UpdateManager::normalizeVersion(const std::string &value) {
    std::string result = value;
    while (!result.empty() && (result[0] == 'v' || result[0] == 'V')) result.erase(result.begin());
    const auto position = result.find_first_of("-+");
    if (position != std::string::npos) result.resize(position);
    return result;
}

bool UpdateManager::isNewerVersion(const std::string &candidate, const std::string &current) {
    std::array<int, 3> candidateParts{};
    std::array<int, 3> currentParts{};
    return parseVersion(candidate, candidateParts) && parseVersion(current, currentParts) &&
           candidateParts > currentParts;
}

void UpdateManager::checkForUpdates() {
    joinWorker();
    m_worker = std::thread([this] {
        std::string latestVersion;
        std::wstring error;
        if (!fetchLatestVersion(latestVersion, error)) {
            if (m_callbacks.checkError) m_callbacks.checkError(error);
            return;
        }
        if (isNewerVersion(latestVersion, m_currentVersion)) {
            if (m_callbacks.updateAvailable) m_callbacks.updateAvailable(latestVersion);
        } else if (m_callbacks.noUpdateAvailable) {
            m_callbacks.noUpdateAvailable();
        }
    });
}
