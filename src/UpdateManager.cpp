#include "UpdateManager.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <sstream>

namespace {

constexpr wchar_t kGitHubHost[] = L"api.github.com";
constexpr wchar_t kLatestReleasePath[] = L"/repos/jwwsjlm/ShutDown/releases/latest";

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

bool fetchLatestVersion(std::string &version, std::wstring &error) {
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

    InternetHandle connection(WinHttpConnect(session.get(), kGitHubHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.get()) {
        error = requestError(L"无法连接 GitHub");
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", kLatestReleasePath,
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
        error = requestError(L"GitHub 更新检查失败");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        error = requestError(L"无法读取 GitHub 响应");
        return false;
    }
    if (status != 200) {
        error = L"GitHub 更新检查失败（HTTP " + std::to_wstring(status) + L"）";
        return false;
    }

    std::string body;
    std::array<char, 4096> buffer{};
    for (;;) {
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
            error = requestError(L"无法读取 GitHub 响应");
            return false;
        }
        if (bytesRead == 0) break;
        body.append(buffer.data(), bytesRead);
        if (body.size() > 1024 * 1024) {
            error = L"GitHub 返回的版本信息过大";
            return false;
        }
    }

    if (!readJsonString(body, "tag_name", version)) {
        error = L"GitHub 返回的版本信息无效";
        return false;
    }
    version = UpdateManager::normalizeVersion(version);
    std::array<int, 3> parsed{};
    if (!parseVersion(version, parsed)) {
        error = L"GitHub Release 版本号格式无效";
        return false;
    }
    return true;
}

} // namespace

UpdateManager::UpdateManager(std::string currentVersion) : m_currentVersion(std::move(currentVersion)) {}
UpdateManager::~UpdateManager() { joinWorker(); }

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
