#include "UpdateManager.h"

#include <cstdio>

int main() {
    int failures = 0;
    auto fail = [&failures](const char *message) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; };

    const auto arch = UpdateManager::currentArchitectureToken();
    if (arch != "x64" && arch != "x86") fail("architecture token");
    if (UpdateManager::normalizeVersion("v2.1.0-beta") != "2.1.0") fail("version normalize prerelease");
    if (UpdateManager::normalizeVersion("V2.1.1+build.7") != "2.1.1") fail("version normalize metadata");
    if (UpdateManager::normalizeVersion("2.1.2") != "2.1.2") fail("version normalize plain");

    if (failures == 0) { std::puts("Win32 update tests: PASS"); return 0; }
    return 1;
}