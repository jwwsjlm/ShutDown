#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QMetaType>
#include <QUrl>
#include <QByteArray>
#include <QList>
#include <QString>

class QFile;
class QJsonObject;

struct UpdateInfo {
    QString version;
    QString tagName;
    QString title;
    QString notes;
    QString assetName;
    QUrl downloadUrl;
    QList<QUrl> mirrorUrls;
    QByteArray sha256;

    bool isValid() const { return !version.isEmpty() && downloadUrl.isValid(); }
};

Q_DECLARE_METATYPE(UpdateInfo)

class UpdateManager final : public QObject {
    Q_OBJECT
public:
    explicit UpdateManager(QObject *parent = nullptr);

    void checkForUpdates();
    void downloadUpdate(const UpdateInfo &info);
    void cancelDownload();
    bool installAndRestart(const QString &downloadedFile, QString *errorMessage = nullptr) const;

signals:
    void checkingStarted(int requestCount);
    void checkingFinished();
    void updateAvailable(const UpdateInfo &info);
    void noUpdateAvailable();
    void checkError(const QString &message);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &filePath);
    void downloadError(const QString &message);

private:
    struct CheckResult {
        UpdateInfo info;
        QString source;
    };

    QList<QUrl> checkUrls() const;
    QList<QUrl> downloadUrls(const UpdateInfo &info) const;
    bool parseResponse(const QByteArray &data, const QString &source, UpdateInfo *info) const;
    bool parseRelease(const QJsonObject &object, UpdateInfo *info) const;
    bool parseDescriptor(const QJsonObject &object, UpdateInfo *info) const;
    void finishChecking();
    void startNextDownload();
    bool verifyDownloadedFile(const QString &path, const QByteArray &expectedSha256) const;
    static QString normalizeVersion(const QString &value);
    static bool isNewerThanCurrent(const QString &version);

    QNetworkAccessManager m_network;
    QHash<QNetworkReply *, QString> m_checkReplies;
    QList<CheckResult> m_checkResults;
    int m_completedChecks = 0;
    int m_totalChecks = 0;

    UpdateInfo m_downloadInfo;
    QList<QUrl> m_downloadCandidates;
    int m_downloadIndex = 0;
    QNetworkReply *m_downloadReply = nullptr;
    QFile *m_downloadFile = nullptr;
    QString m_downloadPath;
};
