#include "UpdateManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVersionNumber>

namespace {
constexpr auto kOwner = "jwwsjlm";
constexpr auto kRepository = "ShutDown";
constexpr auto kUserAgent = "ShutDown-Updater/1.0";
constexpr auto kApiPath = "/repos/jwwsjlm/ShutDown/releases/latest";
constexpr auto kDescriptorPath = "gh/jwwsjlm/ShutDown@main/update.json";

    const QStringList kGenericMirrors{
    QStringLiteral("https://gh.jasonzeng.dev/"),
    QStringLiteral("https://ghproxy.monkeyray.net/"),
    QStringLiteral("https://gh-proxy.com/"),
    QStringLiteral("https://cdn.akaere.online/"),
    QStringLiteral("https://git.yylx.win/")
};

const QStringList kJsDelivrMirrors{
    QStringLiteral("https://fastly.jsdelivr.net/"),
    QStringLiteral("https://testingcf.jsdelivr.net/"),
    QStringLiteral("https://cdn.jsdelivr.net/")
};

QString stripVersionPrefix(QString value) {
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) value.remove(0, 1);
    return value;
}
}

UpdateManager::UpdateManager(QObject *parent) : QObject(parent) {}

QList<QUrl> UpdateManager::checkUrls() const {
    QList<QUrl> urls;
    urls << QUrl(QStringLiteral("https://api.github.com") + QString::fromLatin1(kApiPath));
    for (const auto &mirror : kGenericMirrors) {
        urls << QUrl(mirror + QStringLiteral("https://api.github.com") + QString::fromLatin1(kApiPath));
    }
    for (const auto &mirror : kJsDelivrMirrors) {
        urls << QUrl(mirror + QString::fromLatin1(kDescriptorPath));
    }
    urls << QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/%2/main/update.json").arg(QString::fromLatin1(kOwner), QString::fromLatin1(kRepository)));
    return urls;
}

QList<QUrl> UpdateManager::downloadUrls(const UpdateInfo &info) const {
    QList<QUrl> urls;
    if (info.downloadUrl.isValid()) urls << info.downloadUrl;
    for (const auto &mirror : info.mirrorUrls) {
        if (!urls.contains(mirror)) urls << mirror;
    }
    if (!info.tagName.isEmpty() && !info.assetName.isEmpty()) {
        const QStringList cdnHosts{
            QStringLiteral("https://fastly.jsdelivr.net/"),
            QStringLiteral("https://testingcf.jsdelivr.net/"),
            QStringLiteral("https://cdn.jsdelivr.net/")};
        for (const auto &host : cdnHosts) {
            const QUrl mirror(host + QStringLiteral("gh/%1/%2@%3/%4")
                              .arg(QString::fromLatin1(kOwner), QString::fromLatin1(kRepository), info.tagName, info.assetName));
            if (!urls.contains(mirror)) urls << mirror;
        }
    }
    return urls;
}

void UpdateManager::checkForUpdates() {
    if (!m_checkReplies.isEmpty()) return;
    m_checkResults.clear();
    m_completedChecks = 0;
    const auto urls = checkUrls();
    m_totalChecks = urls.size();
    emit checkingStarted(m_totalChecks);

    for (const auto &url : urls) {
        QNetworkRequest request(url);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
#else
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
        request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
        request.setRawHeader("Accept", "application/vnd.github+json");
        auto *reply = m_network.get(request);
        m_checkReplies.insert(reply, url.toString());
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QString source = m_checkReplies.take(reply);
            if (reply->error() == QNetworkReply::NoError) {
                UpdateInfo info;
                if (parseResponse(reply->readAll(), source, &info) && isNewerThanCurrent(info.version)) {
                    m_checkResults.append({info, source});
                }
            } else {
                qWarning() << "Update check failed from" << source << reply->errorString();
            }
            reply->deleteLater();
            ++m_completedChecks;
            if (m_completedChecks >= m_totalChecks) finishChecking();
        });
        QTimer::singleShot(12000, reply, [reply] {
            if (!reply->isFinished()) reply->abort();
        });
    }
}

void UpdateManager::finishChecking() {
    emit checkingFinished();
    if (m_checkResults.isEmpty()) {
        emit checkError(QStringLiteral("未能从 GitHub 或代理源获取有效的 Release 信息。请确认仓库已有 Release，并检查网络连接。"));
        return;
    }
    auto best = m_checkResults.first().info;
    for (const auto &result : m_checkResults) {
        if (QVersionNumber::fromString(normalizeVersion(result.info.version)) >
            QVersionNumber::fromString(normalizeVersion(best.version))) {
            best = result.info;
        }
    }
    emit updateAvailable(best);
}

bool UpdateManager::parseResponse(const QByteArray &data, const QString &source, UpdateInfo *info) const {
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (object.contains(QStringLiteral("tag_name"))) return parseRelease(object, info);
    if (object.contains(QStringLiteral("version"))) return parseDescriptor(object, info);
    qWarning() << "Unrecognized update response from" << source;
    return false;
}

bool UpdateManager::parseRelease(const QJsonObject &object, UpdateInfo *info) const {
    const QString tag = object.value(QStringLiteral("tag_name")).toString();
    const QString version = normalizeVersion(tag);
    if (version.isEmpty()) return false;
    info->version = version;
    info->tagName = tag;
    info->title = object.value(QStringLiteral("name")).toString();
    info->notes = object.value(QStringLiteral("body")).toString();

    const auto assets = object.value(QStringLiteral("assets")).toArray();
    QJsonObject selected;
    for (const auto &value : assets) {
        const auto asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) || name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            if (selected.isEmpty() || name.contains(QStringLiteral("windows"), Qt::CaseInsensitive)) selected = asset;
        }
    }
    if (selected.isEmpty()) {
        // A release without an uploaded asset can still be reported, but it
        // cannot be downloaded automatically.
        return false;
    }
    info->assetName = selected.value(QStringLiteral("name")).toString();
    info->downloadUrl = QUrl(selected.value(QStringLiteral("browser_download_url")).toString());
    const QString digest = selected.value(QStringLiteral("digest")).toString();
    if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) info->sha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
    for (const auto &mirror : kGenericMirrors) info->mirrorUrls << QUrl(mirror + info->downloadUrl.toString());
    return info->isValid();
}

bool UpdateManager::parseDescriptor(const QJsonObject &object, UpdateInfo *info) const {
    info->version = normalizeVersion(object.value(QStringLiteral("version")).toString());
    info->tagName = object.value(QStringLiteral("tag")).toString();
    info->title = object.value(QStringLiteral("title")).toString();
    info->notes = object.value(QStringLiteral("notes")).toString();
    info->assetName = object.value(QStringLiteral("asset")).toString();
    info->downloadUrl = QUrl(object.value(QStringLiteral("download")).toString());
    if (!info->downloadUrl.isValid()) info->downloadUrl = QUrl(object.value(QStringLiteral("downloadUrl")).toString());
    const QString digest = object.value(QStringLiteral("sha256")).toString();
    info->sha256 = QByteArray::fromHex(digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive) ? digest.mid(7).toLatin1() : digest.toLatin1());
    for (const auto &mirror : kGenericMirrors) info->mirrorUrls << QUrl(mirror + info->downloadUrl.toString());
    return info->isValid();
}

void UpdateManager::downloadUpdate(const UpdateInfo &info) {
    cancelDownload();
    m_downloadInfo = info;
    m_downloadCandidates = downloadUrls(info);
    m_downloadIndex = 0;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/ShutDown/update");
    QDir().mkpath(dir);
    const QString name = info.assetName.isEmpty() ? QStringLiteral("ShutDown-update.bin") : info.assetName;
    // Keep the original extension so the detached installer can distinguish
    // a standalone EXE from a ZIP package.
    m_downloadPath = dir + QStringLiteral("/") + name;
    startNextDownload();
}

void UpdateManager::startNextDownload() {
    if (m_downloadIndex >= m_downloadCandidates.size()) {
        emit downloadError(QStringLiteral("所有 GitHub 下载地址均不可用"));
        return;
    }
    const QUrl url = m_downloadCandidates.at(m_downloadIndex++);
    delete m_downloadFile;
    m_downloadFile = new QFile(m_downloadPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadError(QStringLiteral("无法创建更新文件：%1").arg(m_downloadPath));
        return;
    }
    QNetworkRequest request(url);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
#else
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    m_downloadReply = m_network.get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        if (m_downloadFile) m_downloadFile->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &UpdateManager::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, [this] {
        if (m_downloadFile) {
            m_downloadFile->write(m_downloadReply->readAll());
            m_downloadFile->flush();
            m_downloadFile->close();
        }
        const bool ok = m_downloadReply->error() == QNetworkReply::NoError && verifyDownloadedFile(m_downloadPath, m_downloadInfo.sha256);
        const QString error = m_downloadReply->errorString();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
        if (ok) {
            emit downloadFinished(m_downloadPath);
        } else {
            qWarning() << "Update download failed:" << error;
            startNextDownload();
        }
    });
    QTimer::singleShot(60000, m_downloadReply, [this] {
        if (m_downloadReply && !m_downloadReply->isFinished()) m_downloadReply->abort();
    });
}

void UpdateManager::cancelDownload() {
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    delete m_downloadFile;
    m_downloadFile = nullptr;
}

bool UpdateManager::verifyDownloadedFile(const QString &path, const QByteArray &expectedSha256) const {
    if (expectedSha256.isEmpty()) return QFileInfo::exists(path) && QFileInfo(path).size() > 0;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray actual = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    return actual == expectedSha256;
}

QString UpdateManager::normalizeVersion(const QString &value) {
    return stripVersionPrefix(value).section(QRegularExpression(QStringLiteral("[-+]")), 0, 0).trimmed();
}

bool UpdateManager::isNewerThanCurrent(const QString &version) {
    const auto current = QVersionNumber::fromString(normalizeVersion(QCoreApplication::applicationVersion()));
    const auto candidate = QVersionNumber::fromString(normalizeVersion(version));
    return !candidate.isNull() && candidate > current;
}

bool UpdateManager::installAndRestart(const QString &downloadedFile, QString *errorMessage) const {
#ifdef Q_OS_WIN
    const QString scriptPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/ShutDown/update/install.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建更新脚本");
        return false;
    }
    QTextStream out(&script);
    out << "$ErrorActionPreference = 'Stop'\n"
           "$current = $args[0]\n$downloaded = $args[1]\n$pid = [int]$args[2]\n"
           "while (Get-Process -Id $pid -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }\n"
           "$work = Join-Path ([IO.Path]::GetTempPath()) ('ShutDown-update-' + [guid]::NewGuid().ToString())\n"
           "New-Item -ItemType Directory -Path $work -Force | Out-Null\n"
           "$source = $downloaded\n"
           "if ([IO.Path]::GetExtension($downloaded) -ieq '.zip') { Expand-Archive -LiteralPath $downloaded -DestinationPath $work -Force; $source = Join-Path $work 'ShutDown.exe' }\n"
           "if (!(Test-Path $source)) { throw 'ShutDown.exe not found in update package' }\n"
           "Copy-Item -LiteralPath $source -Destination $current -Force\n"
           "Start-Process -FilePath $current\n"
           "Remove-Item -LiteralPath $downloaded -Force -ErrorAction SilentlyContinue\n"
           "Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue\n"
           "Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue\n";
    script.close();
    const QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-File"), scriptPath, QCoreApplication::applicationFilePath(), downloadedFile, QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法启动更新脚本");
        return false;
    }
    return true;
#else
    Q_UNUSED(downloadedFile);
    if (errorMessage) *errorMessage = QStringLiteral("当前平台不支持自动替换程序");
    return false;
#endif
}
