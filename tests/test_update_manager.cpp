#include "UpdateManager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <cstdio>

class UpdateManagerTest {
public:
    static int run() {
    int failures = 0;
    const auto fail = [&failures](const char *message) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    };

    UpdateManager manager;
    QJsonObject release;
    release.insert(QStringLiteral("tag_name"), QStringLiteral("v1.0.9"));
    release.insert(QStringLiteral("name"), QStringLiteral("test release"));
    QJsonArray assets;
    assets.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("ShutDown-windows-x64.exe")},
                              {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/x64.exe")}});
    assets.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("ShutDown-windows-x86.exe")},
                              {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/x86.exe")}});
    release.insert(QStringLiteral("assets"), assets);

    UpdateInfo info;
    if (!manager.parseRelease(release, &info)) fail("architecture asset selection");
    const QString expected = QSysInfo::WordSize == 32
        ? QStringLiteral("ShutDown-windows-x86.exe")
        : QStringLiteral("ShutDown-windows-x64.exe");
    if (info.assetName != expected) fail("selected asset matches process architecture");

    bool noUpdate = false;
    QObject::connect(&manager, &UpdateManager::noUpdateAvailable, [&noUpdate] { noUpdate = true; });
    manager.m_successfulChecks = 1;
    manager.m_checkResults.clear();
    manager.finishChecking();
    if (!noUpdate) fail("valid response without newer version reports no update");

        return failures == 0 ? 0 : 1;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    return UpdateManagerTest::run();
}
