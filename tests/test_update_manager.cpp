#include "UpdateManager.h"

#include <algorithm>
#include <cstdio>

int main() {
    int failures = 0;
    auto fail = [&failures](const char *message) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; };
    const std::string release = R"({"tag_name":"v1.0.11","name":"test","assets":[{"name":"ShutDown-windows-x64.zip","browser_download_url":"https://example.invalid/x64.zip"},{"name":"ShutDown-windows-x86.zip","browser_download_url":"https://example.invalid/x86.zip"}]})";
    UpdateInfo info;
    if (!UpdateManager::parseRelease(release, &info)) fail("release parse");
    const std::string expected = UpdateManager::currentArchitectureToken() == "x64" ? "ShutDown-windows-x64.zip" : "ShutDown-windows-x86.zip";
    if (info.assetName != expected) fail("architecture asset selection");
    const std::string githubRelease = R"({"tag_name":"v2.0.5","assets":[{"name":"ShutDown-windows-x64.zip","browser_download_url":"https://github.com/jwwsjlm/ShutDown/releases/download/v2.0.5/ShutDown-windows-x64.zip"},{"name":"ShutDown-windows-x86.zip","browser_download_url":"https://github.com/jwwsjlm/ShutDown/releases/download/v2.0.5/ShutDown-windows-x86.zip"}]})";
    UpdateInfo mirrorInfo;
    if (!UpdateManager::parseRelease(githubRelease, &mirrorInfo)) fail("github mirror parse");
    auto hasMirror = [&mirrorInfo](const std::string &prefix) {
        return std::any_of(mirrorInfo.mirrorUrls.begin(), mirrorInfo.mirrorUrls.end(), [&prefix](const std::string &url) {
            return url.rfind(prefix, 0) == 0;
        });
    };
    if (!hasMirror("https://fastly.jsdelivr.net/")) fail("fastly jsdelivr mirror");
    if (!hasMirror("https://testingcf.jsdelivr.net/")) fail("testingcf jsdelivr mirror");
    if (!hasMirror("https://cdn.jsdelivr.net/")) fail("cdn jsdelivr mirror");
    if (!hasMirror("https://git.yylx.win/")) fail("git yylx mirror");
    const std::string page = "<a href=\"/jwwsjlm/ShutDown/releases/tag/v2.0.2\">v2.0.2</a> ShutDown-windows-" + UpdateManager::currentArchitectureToken() + ".zip";
    UpdateInfo pageInfo;
    if (!UpdateManager::parseReleasePage(page, &pageInfo) || pageInfo.version != "2.0.2") fail("release page parse");
    if (UpdateManager::normalizeVersion("v2.1.0-beta") != "2.1.0") fail("version normalize");
    if (failures == 0) { std::puts("Win32 update tests: PASS"); return 0; }
    return 1;
}
