#pragma once

#include "ShutdownScheduler.h"
#include "UpdateManager.h"

#include <QMainWindow>
#include <QSystemTrayIcon>

class QCheckBox;
class QLabel;
class QDateTimeEdit;
class QSpinBox;
class QSystemTrayIcon;
class QAction;
class QPushButton;
class QMenu;
class QProgressBar;
class QVBoxLayout;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void scheduleAt();
    void scheduleCountdown();
    void cancelTask();
    void togglePause();
    void showFromTray();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void updateRemaining(qint64 seconds);
    void updateState(ShutdownScheduler::State state);
    void executeNow();
    void checkForUpdates();
    void handleUpdateAvailable(const UpdateInfo &info);
    void handleNoUpdateAvailable();
    void handleUpdateCheckError(const QString &message);
    void handleDownloadProgress(qint64 received, qint64 total);
    void handleDownloadFinished(const QString &filePath);
    void handleDownloadError(const QString &message);

private:
    void buildUi();
    void buildTray();
    void restorePersistedTask();
    QString formatDuration(qint64 seconds) const;
    bool askCloseWithActiveTask();
    void addUpdateControls(QVBoxLayout *root);

    ShutdownScheduler m_scheduler;
    UpdateManager m_updateManager;
    UpdateInfo m_availableUpdate;
    QDateTimeEdit *m_dateTimeEdit = nullptr;
    QSpinBox *m_hours = nullptr;
    QSpinBox *m_minutes = nullptr;
    QSpinBox *m_seconds = nullptr;
    QCheckBox *m_force = nullptr;
    QCheckBox *m_fallback = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_remaining = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_checkUpdateAction = nullptr;
    QPushButton *m_checkUpdateButton = nullptr;
    QProgressBar *m_updateProgress = nullptr;
    bool m_forceQuit = false;
    bool m_silentUpdateCheck = false;
};
