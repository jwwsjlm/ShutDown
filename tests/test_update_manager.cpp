#include "UpdateManager.h"

#include <cstdio>

int main() {
    int failures = 0;
    auto fail = [&failures](const char *message) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; };
    const std::string release = R"({"tag_name":"v1.0.11","name":"test","assets":[{"name":"ShutDown-windows-x64.zip","browser_download_url":"https://example.invalid/x64.zip"},{"name":"ShutDown-windows-x86.zip","browser_download_url":"https://example.invalid/x86.zip"}]})";
    UpdateInfo info;
    if (!UpdateManager::parseRelease(release, &info)) fail("release parse");
    const std::string expected = UpdateManager::currentArchitectureToken() == "x64" ? "ShutDown-windows-x64.zip" : "ShutDown-windows-x86.zip";
    if (info.assetName != expected) fail("architecture asset selection");
    if (UpdateManager::normalizeVersion("v2.1.0-beta") != "2.1.0") fail("version normalize");
    if (failures == 0) { std::puts("Win32 update tests: PASS"); return 0; }
    return 1;
}
