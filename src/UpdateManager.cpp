#include "UpdateManager.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

namespace {
constexpr char kOwner[] = "jwwsjlm";
constexpr char kRepository[] = "ShutDown";
constexpr char kApiPath[] = "/repos/jwwsjlm/ShutDown/releases/latest";
const std::vector<std::string> kGithubProxyPrefixes{
    "https://fastly.jsdelivr.net/",
    "https://testingcf.jsdelivr.net/",
    "https://cdn.jsdelivr.net/",
    "https://git.yylx.win/",
    "https://gh.jasonzeng.dev/",
    "https://ghproxy.monkeyray.net/",
    "https://gh-proxy.com/",
    "https://cdn.akaere.online/"
};

bool isGithubUrl(const std::string &url) {
    return url.rfind("https://github.com/", 0) == 0 || url.rfind("http://github.com/", 0) == 0;
}

void appendGithubProxyUrls(const std::string &githubUrl, std::vector<std::string> *urls) {
    if (!urls || !isGithubUrl(githubUrl)) return;
    for (const auto &prefix : kGithubProxyPrefixes) urls->push_back(prefix + githubUrl);
}

std::wstring utf8ToWide(const std::string &value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string unescapeJson(std::string value) {
    std::string output;
    output.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) { output += value[i]; continue; }
        const char c = value[++i];
        switch (c) {
        case 'n': output += '\n'; break;
        case 'r': output += '\n'; break;
        case 't': output += '\t'; break;
        case '"': output += '"'; break;
        case '\\': output += '\\'; break;
        case '/': output += '/'; break;
        default: output += c; break;
        }
    }
    return output;
}

std::string jsonString(const std::string &json, const std::string &key) {
    const std::string marker = "\"" + key + "\"";
    size_t position = json.find(marker);
    if (position == std::string::npos) return {};
    position = json.find(':', position + marker.size());
    if (position == std::string::npos) return {};
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t' || json[position] == '\n' || json[position] == '\n')) ++position;
    if (position >= json.size() || json[position] != '\"') return {};
    ++position;
    std::string raw;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char c = json[position];
        if (!escaped && c == '\"') break;
        if (!escaped && c == '\\') escaped = true;
        else escaped = false;
        raw += c;
    }
    return unescapeJson(raw);
}

bool httpGet(const std::string &url, std::vector<unsigned char> *data, std::function<void(std::int64_t, std::int64_t)> progress = {}) {
    if (!data) return false;
    data->clear();
    const std::wstring wideUrl = utf8ToWide(url);
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    wchar_t host[256]{}, path[4096]{}, extra[2048]{};
    components.lpszHostName = host;
    components.dwHostNameLength = _countof(host);
    components.lpszUrlPath = path;
    components.dwUrlPathLength = _countof(path);
    components.lpszExtraInfo = extra;
    components.dwExtraInfoLength = _countof(extra);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) return false;

    HINTERNET session = WinHttpOpen(L"ShutDown-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session && GetLastError() == ERROR_INVALID_PARAMETER) {
        // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY is not available on Windows 7.
        session = WinHttpOpen(L"ShutDown-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) return false;
    // 更新检查要快速失败并切换备用源，避免点击一次长时间无响应。
    WinHttpSetTimeouts(session, 2500, 2500, 2500, 3500);
    HINTERNET connection = WinHttpConnect(session, host, components.nPort, 0);
    if (!connection) { WinHttpCloseHandle(session); return false; }
    std::wstring requestPath = path;
    requestPath += extra;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request) { WinHttpCloseHandle(connection); WinHttpCloseHandle(session); return false; }
    if (components.nScheme == INTERNET_SCHEME_HTTPS) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                          WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                          WINHTTP_FLAG_SECURE_PROTOCOL_TLS1;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json\n", -1, WINHTTP_ADDREQ_FLAG_ADD);
    const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
                      WinHttpReceiveResponse(request, nullptr);
    if (!sent) { WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session); return false; }
    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    if (status < 200 || status >= 300) { WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session); return false; }
    DWORD contentLength = 0, lengthSize = sizeof(contentLength);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &contentLength, &lengthSize, nullptr);
    std::int64_t received = 0;
    bool readOk = true;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) { readOk = false; break; }
        if (!available) break;
        const size_t oldSize = data->size();
        data->resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, data->data() + oldSize, available, &read)) { readOk = false; data->clear(); break; }
        data->resize(oldSize + read);
        received += read;
        if (progress) progress(received, contentLength);
    }
    const bool ok = readOk && !data->empty();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

std::vector<unsigned char> sha256(const std::vector<unsigned char> &data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0, resultSize = 0, hashSize = 0;
    std::vector<unsigned char> result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return result;
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0);
    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &resultSize, 0);
    std::vector<unsigned char> object(objectSize); result.resize(hashSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0 &&
        BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) == 0)
        BCryptFinishHash(hash, result.data(), hashSize, 0);
    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}
}

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
    auto parse = [](const std::string &s) { std::array<int, 3> out{}; std::stringstream ss(s); char dot; ss >> out[0] >> dot >> out[1] >> dot >> out[2]; return out; };
    return parse(normalizeVersion(version)) > parse(normalizeVersion(m_currentVersion));
}

bool UpdateManager::parseRelease(const std::string &object, UpdateInfo *info) {
    if (!info) return false;
    *info = UpdateInfo{};
    info->version = normalizeVersion(jsonString(object, "tag_name"));
    info->tagName = jsonString(object, "tag_name");
    info->title = jsonString(object, "name");
    info->notes = jsonString(object, "body");
    if (info->version.empty()) return false;

    UpdateInfo zipCandidate;
    size_t cursor = 0;
    while ((cursor = object.find("\"name\"", cursor)) != std::string::npos) {
        const size_t nameColon = object.find(':', cursor + 6);
        if (nameColon == std::string::npos) break;
        const size_t nameStart = object.find('\"', nameColon + 1);
        if (nameStart == std::string::npos) break;
        const size_t nameEnd = object.find('\"', nameStart + 1);
        if (nameEnd == std::string::npos) break;
        const std::string name = unescapeJson(object.substr(nameStart + 1, nameEnd - nameStart - 1));
        cursor = nameEnd + 1;
        if (name.find("-" + currentArchitectureToken()) == std::string::npos) continue;
        if (name.find(".zip") == std::string::npos && name.find(".exe") == std::string::npos) continue;

        const size_t objectEnd = object.find('}', nameEnd);
        const size_t urlKey = object.find("\"browser_download_url\"", nameEnd);
        if (urlKey == std::string::npos || (objectEnd != std::string::npos && urlKey > objectEnd)) continue;
        const size_t urlColon = object.find(':', urlKey);
        const size_t urlStart = urlColon == std::string::npos ? std::string::npos : object.find('\"', urlColon + 1);
        const size_t urlEnd = urlStart == std::string::npos ? std::string::npos : object.find('\"', urlStart + 1);
        if (urlStart == std::string::npos || urlEnd == std::string::npos) continue;
        UpdateInfo candidate = *info;
        candidate.assetName = name;
        candidate.downloadUrl = unescapeJson(object.substr(urlStart + 1, urlEnd - urlStart - 1));

        const size_t digestKey = object.find("\"digest\"", nameEnd);
        if (digestKey != std::string::npos && (objectEnd == std::string::npos || digestKey < objectEnd)) {
            const size_t digestColon = object.find(':', digestKey);
            const size_t digestStart = digestColon == std::string::npos ? std::string::npos : object.find('\"', digestColon + 1);
            const size_t digestEnd = digestStart == std::string::npos ? std::string::npos : object.find('\"', digestStart + 1);
            if (digestStart != std::string::npos && digestEnd != std::string::npos) {
                std::string digest = object.substr(digestStart + 1, digestEnd - digestStart - 1);
                if (digest.rfind("sha256:", 0) == 0) digest = digest.substr(7);
                if (digest.size() == 64) {
                    for (size_t i = 0; i < digest.size(); i += 2)
                        candidate.sha256.push_back(static_cast<unsigned char>(std::stoi(digest.substr(i, 2), nullptr, 16)));
                }
            }
        }
        appendGithubProxyUrls(candidate.downloadUrl, &candidate.mirrorUrls);
        if (name.find(".exe") != std::string::npos) {
            *info = candidate;
            return info->isValid();
        }
        if (!zipCandidate.isValid()) zipCandidate = candidate;
    }
    if (zipCandidate.isValid()) {
        *info = zipCandidate;
        return true;
    }
    return false;
}

bool UpdateManager::parseReleasePage(const std::string &html, UpdateInfo *info) {
    if (!info) return false;
    *info = UpdateInfo{};
    const std::string tagMarker = "/releases/tag/";
    const size_t tagPos = html.find(tagMarker);
    if (tagPos == std::string::npos) return false;
    const size_t tagStart = tagPos + tagMarker.size();
    size_t tagEnd = tagStart;
    while (tagEnd < html.size() && html[tagEnd] != '"' && html[tagEnd] != '\'' && html[tagEnd] != '<' && html[tagEnd] != '/') ++tagEnd;
    if (tagEnd == tagStart) return false;
    info->tagName = html.substr(tagStart, tagEnd - tagStart);
    info->version = normalizeVersion(info->tagName);
    info->title = "GitHub Release";
    std::string asset = "ShutDown-windows-" + currentArchitectureToken() + ".exe";
    if (html.find(asset) == std::string::npos) {
        asset = "ShutDown-windows-" + currentArchitectureToken() + ".zip";
        if (html.find(asset) == std::string::npos) return false;
    }
    info->assetName = asset;
    info->downloadUrl = "https://github.com/jwwsjlm/ShutDown/releases/download/" + info->tagName + "/" + asset;
    return info->isValid();
}

bool UpdateManager::parseDescriptor(const std::string &object, UpdateInfo *info) {
    if (!info) return false;
    info->version = normalizeVersion(jsonString(object, "version"));
    info->tagName = jsonString(object, "tag"); info->title = jsonString(object, "title");
    info->notes = jsonString(object, "notes"); info->assetName = jsonString(object, "asset");
    info->downloadUrl = jsonString(object, "download");
    if (info->downloadUrl.empty()) info->downloadUrl = jsonString(object, "downloadUrl");
    const std::string arch = currentArchitectureToken();
    const std::string archAsset = jsonString(object, "asset_" + arch);
    const std::string archDownload = jsonString(object, "download_" + arch);
    const std::string archDigest = jsonString(object, "sha256_" + arch);
    if (!archAsset.empty()) info->assetName = archAsset;
    if (!archDownload.empty()) info->downloadUrl = archDownload;
    const std::string digest = archDigest.empty() ? jsonString(object, "sha256") : archDigest;
    const std::string clean = digest.rfind("sha256:", 0) == 0 ? digest.substr(7) : digest;
    for (size_t i = 0; i + 1 < clean.size(); i += 2) info->sha256.push_back(static_cast<unsigned char>(std::stoi(clean.substr(i, 2), nullptr, 16)));
    appendGithubProxyUrls(info->downloadUrl, &info->mirrorUrls);
    return info->isValid();
}

void UpdateManager::checkForUpdates() {
    joinWorker(); m_cancel = false;
    if (m_callbacks.checkingStarted) m_callbacks.checkingStarted();
    m_worker = std::thread([this] {
        std::vector<std::string> urls{"https://api.github.com" + std::string(kApiPath),
                                      "https://github.com/jwwsjlm/ShutDown/releases/latest"};
        appendGithubProxyUrls("https://github.com/jwwsjlm/ShutDown/releases/latest", &urls);
        std::vector<UpdateInfo> candidates; std::wstring errors;
        bool validSource = false;
        for (const auto &url : urls) {
            if (m_cancel) return;
            std::vector<unsigned char> body;
            if (!httpGet(url, &body)) { errors += utf8ToWide(url) + L": HTTP/TLS 请求失败\n"; continue; }
            const std::string text(body.begin(), body.end()); UpdateInfo info;
            if ((text.find("\"tag_name\"") != std::string::npos && parseRelease(text, &info)) ||
                (text.find("/releases/tag/") != std::string::npos && parseReleasePage(text, &info))) {
                validSource = true;
                if (isNewerThanCurrent(info.version)) candidates.push_back(info);
                break;
            } else errors += utf8ToWide(url) + L": JSON 解析失败或缺少当前架构资产\n";
        }
        if (m_callbacks.checkingFinished) m_callbacks.checkingFinished();
        if (!candidates.empty()) {
            const auto best = *std::max_element(candidates.begin(), candidates.end(), [](const UpdateInfo &a, const UpdateInfo &b) { return a.version < b.version; });
            if (m_callbacks.updateAvailable) m_callbacks.updateAvailable(best);
        } else if (validSource || errors.empty()) {
            if (m_callbacks.noUpdateAvailable) m_callbacks.noUpdateAvailable();
        } else if (m_callbacks.checkError) m_callbacks.checkError(L"未能从 GitHub 或代理源获取有效的 Release 信息。\n\n" + errors);
    });
}

void UpdateManager::downloadUpdate(const UpdateInfo &info) {
    joinWorker(); m_cancel = false;
    m_worker = std::thread([this, info] {
        wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
        const std::filesystem::path dir = std::filesystem::path(temp) / L"ShutDown" / L"update";
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        const std::wstring fileName = utf8ToWide(info.assetName.empty() ? "ShutDown-update.zip" : info.assetName);
        const auto path = dir / fileName;
        std::vector<std::string> urls{info.downloadUrl}; urls.insert(urls.end(), info.mirrorUrls.begin(), info.mirrorUrls.end());
        for (const auto &url : urls) {
            std::vector<unsigned char> data;
            if (httpGet(url, &data, [this](std::int64_t r, std::int64_t t) { if (m_callbacks.downloadProgress) m_callbacks.downloadProgress(r, t); })) {
                if (info.sha256.empty() || sha256(data) == info.sha256) {
                    std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                    if (m_callbacks.downloadFinished) {
                        m_callbacks.downloadFinished(path.wstring());
                    }
                    return;
                }
            }
            if (m_cancel) return;
        }
        if (m_callbacks.downloadError) m_callbacks.downloadError(L"所有 GitHub 下载地址均不可用或校验失败");
    });
}

void UpdateManager::cancelDownload() { m_cancel = true; }

bool UpdateManager::installAndRestart(const std::wstring &downloadedFile, std::wstring *errorMessage) const {
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto script = std::filesystem::path(temp) / L"ShutDown" / L"update" / L"install.ps1";
    std::wofstream file(script);
    if (!file) { if (errorMessage) *errorMessage = L"无法创建更新脚本"; return false; }
    file << L"$ErrorActionPreference='Stop'\n"
            L"$current=$args[0];$downloaded=$args[1];$targetProcessId=[int]$args[2]\n"
            L"$logDir=Join-Path ([IO.Path]::GetTempPath()) 'ShutDown\\update'\n"
            L"New-Item -ItemType Directory -Path $logDir -Force | Out-Null\n"
            L"$log=Join-Path $logDir 'install.log'\n"
            L"function Write-Log($message){Add-Content -LiteralPath $log -Encoding UTF8 -Value ((Get-Date).ToString('s')+' '+$message)}\n"
            L"try{\n"
            L"Write-Log \"install start current=$current downloaded=$downloaded pid=$targetProcessId\"\n"
            L"while(Get-Process -Id $targetProcessId -ErrorAction SilentlyContinue){Start-Sleep -Milliseconds 500}\n"
            L"$work=Join-Path ([IO.Path]::GetTempPath()) ('ShutDown-update-'+[guid]::NewGuid())\n"
            L"New-Item -ItemType Directory -Path $work -Force | Out-Null\n"
            L"$source=$downloaded\n"
            L"if([IO.Path]::GetExtension($downloaded)-ieq '.zip'){\n"
            L"  if(Get-Command Expand-Archive -ErrorAction SilentlyContinue){Expand-Archive -LiteralPath $downloaded -DestinationPath $work -Force}\n"
            L"  else{\n"
            L"    $shell=New-Object -ComObject Shell.Application\n"
            L"    $zip=$shell.NameSpace($downloaded);$dest=$shell.NameSpace($work)\n"
            L"    if($zip -eq $null -or $dest -eq $null){throw 'Cannot open update zip'}\n"
            L"    $dest.CopyHere($zip.Items(), 0x14)\n"
            L"    for($i=0;$i -lt 100 -and -not (Test-Path -LiteralPath (Join-Path $work 'ShutDown.exe'));$i++){Start-Sleep -Milliseconds 200}\n"
            L"  }\n"
            L"  $source=Join-Path $work 'ShutDown.exe'\n"
            L"}\n"
            L"if(-not (Test-Path -LiteralPath $source)){throw \"Update source not found: $source\"}\n"
            L"Copy-Item -LiteralPath $source -Destination $current -Force\n"
            L"Write-Log 'copy complete, restarting'\n"
            L"Start-Process -FilePath $current -WorkingDirectory (Split-Path -Parent $current)\n"
            L"}catch{Write-Log ('install failed: '+$_.Exception.Message);exit 1}\n";
    file.close();
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script.wstring() + L"\" \"" + std::wstring(modulePath) + L"\" \"" + downloadedFile + L"\" " + std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpFile = L"powershell.exe";
    info.lpParameters = args.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) { if (errorMessage) *errorMessage = L"无法启动更新脚本"; return false; }
    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
    return true;
}
