#include "UpdateManager.h"

#include <cstdio>

int main() {
    int failures = 0;
    auto fail = [&failures](const char *message) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; };

    if (UpdateManager::normalizeVersion("v2.1.0-beta") != "2.1.0") fail("version normalize prerelease");
    if (UpdateManager::normalizeVersion("V2.1.1+build.7") != "2.1.1") fail("version normalize metadata");
    if (UpdateManager::normalizeVersion("2.1.2") != "2.1.2") fail("version normalize plain");
    if (!UpdateManager::isNewerVersion("2.2.0", "2.1.9")) fail("newer version");
    if (UpdateManager::isNewerVersion("2.1.4", "2.1.4")) fail("same version");
    if (UpdateManager::isNewerVersion("2.1.3", "2.1.4")) fail("older version");
    if (UpdateManager::isNewerVersion("not-a-version", "2.1.4")) fail("invalid version");

    if (failures == 0) { std::puts("Win32 update tests: PASS"); return 0; }
    return 1;
}
