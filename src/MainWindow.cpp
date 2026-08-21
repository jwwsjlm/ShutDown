#include "MainWindow.h"

#include "SettingsStore.h"
#include "ShutdownExecutor.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("定时关机"));
    setMinimumSize(480, 360);
    buildUi();
    buildTray();
    connect(&m_scheduler, &ShutdownScheduler::remainingChanged, this, &MainWindow::updateRemaining);
    connect(&m_scheduler, &ShutdownScheduler::stateChanged, this, &MainWindow::updateState);
    connect(&m_scheduler, &ShutdownScheduler::executionError, this, [this](const QString &message) {
        QMessageBox::critical(this, QStringLiteral("关机失败"), message);
    });
    connect(&m_updateManager, &UpdateManager::updateAvailable, this, &MainWindow::handleUpdateAvailable);
    connect(&m_updateManager, &UpdateManager::noUpdateAvailable, this, &MainWindow::handleNoUpdateAvailable);
    connect(&m_updateManager, &UpdateManager::checkError, this, &MainWindow::handleUpdateCheckError);
    connect(&m_updateManager, &UpdateManager::downloadProgress, this, &MainWindow::handleDownloadProgress);
    connect(&m_updateManager, &UpdateManager::downloadFinished, this, &MainWindow::handleDownloadFinished);
    connect(&m_updateManager, &UpdateManager::downloadError, this, &MainWindow::handleDownloadError);
    restorePersistedTask();
    QTimer::singleShot(3000, this, [this] {
        m_silentUpdateCheck = true;
        m_updateManager.checkForUpdates();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    auto *atGroup = new QGroupBox(QStringLiteral("指定日期和时间"), central);
    auto *atLayout = new QFormLayout(atGroup);
    m_dateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), atGroup);
    m_dateTimeEdit->setCalendarPopup(true);
    m_dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    atLayout->addRow(QStringLiteral("关机时间:"), m_dateTimeEdit);
    auto *atButton = new QPushButton(QStringLiteral("设置定时关机"), atGroup);
    atLayout->addRow(nullptr, atButton);
    connect(atButton, &QPushButton::clicked, this, &MainWindow::scheduleAt);

    auto *countGroup = new QGroupBox(QStringLiteral("倒计时关机"), central);
    auto *countLayout = new QFormLayout(countGroup);
    auto makeSpin = [countGroup](int max) {
        auto *spin = new QSpinBox(countGroup);
        spin->setRange(0, max);
        return spin;
    };
    m_hours = makeSpin(999);
    m_minutes = makeSpin(59);
    m_seconds = makeSpin(59);
    auto *timeRow = new QWidget(countGroup);
    auto *timeLayout = new QHBoxLayout(timeRow);
    timeLayout->setContentsMargins(0, 0, 0, 0);
    timeLayout->addWidget(m_hours); timeLayout->addWidget(new QLabel(QStringLiteral("小时"), timeRow));
    timeLayout->addWidget(m_minutes); timeLayout->addWidget(new QLabel(QStringLiteral("分钟"), timeRow));
    timeLayout->addWidget(m_seconds); timeLayout->addWidget(new QLabel(QStringLiteral("秒"), timeRow));
    countLayout->addRow(QStringLiteral("时长:"), timeRow);
    auto *countButton = new QPushButton(QStringLiteral("开始倒计时"), countGroup);
    countLayout->addRow(nullptr, countButton);
    connect(countButton, &QPushButton::clicked, this, &MainWindow::scheduleCountdown);

    auto *options = new QGroupBox(QStringLiteral("选项"), central);
    auto *optionsLayout = new QVBoxLayout(options);
    m_force = new QCheckBox(QStringLiteral("强制关闭应用（可能丢失未保存数据）"), options);
    m_fallback = new QCheckBox(QStringLiteral("启用 Task Scheduler 系统兜底"), options);
    optionsLayout->addWidget(m_force); optionsLayout->addWidget(m_fallback);

    auto *statusGroup = new QGroupBox(QStringLiteral("当前任务"), central);
    auto *statusLayout = new QFormLayout(statusGroup);
    m_status = new QLabel(QStringLiteral("空闲"), statusGroup);
    m_remaining = new QLabel(QStringLiteral("--"), statusGroup);
    statusLayout->addRow(QStringLiteral("状态:"), m_status);
    statusLayout->addRow(QStringLiteral("剩余时间:"), m_remaining);

    auto *buttons = new QHBoxLayout;
    m_pauseButton = new QPushButton(QStringLiteral("暂停"), central);
    auto *cancelButton = new QPushButton(QStringLiteral("取消任务"), central);
    auto *nowButton = new QPushButton(QStringLiteral("立即关机"), central);
    buttons->addWidget(m_pauseButton); buttons->addWidget(cancelButton); buttons->addWidget(nowButton);
    m_pauseButton->setEnabled(false);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(cancelButton, &QPushButton::clicked, this, &MainWindow::cancelTask);
    connect(nowButton, &QPushButton::clicked, this, &MainWindow::executeNow);

    root->addWidget(atGroup); root->addWidget(countGroup); root->addWidget(options); root->addWidget(statusGroup); root->addLayout(buttons);
    addUpdateControls(root);
    setCentralWidget(central);
}

void MainWindow::addUpdateControls(QVBoxLayout *root) {
    auto *updateGroup = new QGroupBox(QStringLiteral("软件更新"), this);
    auto *layout = new QVBoxLayout(updateGroup);
    auto *row = new QHBoxLayout;
    m_checkUpdateButton = new QPushButton(QStringLiteral("检查更新"), updateGroup);
    m_updateProgress = new QProgressBar(updateGroup);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setValue(0);
    m_updateProgress->setTextVisible(true);
    m_updateProgress->setFormat(QStringLiteral("未下载更新"));
    row->addWidget(m_checkUpdateButton);
    row->addWidget(m_updateProgress, 1);
    layout->addLayout(row);
    connect(m_checkUpdateButton, &QPushButton::clicked, this, &MainWindow::checkForUpdates);
    root->addWidget(updateGroup);
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon), this);
    m_tray->setToolTip(QStringLiteral("定时关机"));
    m_trayMenu = new QMenu(this);
    m_showAction = m_trayMenu->addAction(QStringLiteral("显示窗口"), this, &MainWindow::showFromTray);
    m_cancelAction = m_trayMenu->addAction(QStringLiteral("取消任务"), this, &MainWindow::cancelTask);
    m_checkUpdateAction = m_trayMenu->addAction(QStringLiteral("检查更新"), this, &MainWindow::checkForUpdates);
    m_trayMenu->addAction(QStringLiteral("立即关机"), this, &MainWindow::executeNow);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("退出"), this, [this] {
        m_forceQuit = true;
        qApp->quit();
    });
    m_tray->setContextMenu(m_trayMenu);
    m_cancelAction->setEnabled(false);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);
    m_tray->show();
}

void MainWindow::restorePersistedTask() {
    if (!SettingsStore::hasTask()) return;
    const auto task = SettingsStore::loadTask();
    if (QMessageBox::question(this, QStringLiteral("恢复任务"), QStringLiteral("检测到上次未完成的关机任务，是否恢复？")) == QMessageBox::Yes) {
        QString error;
        if (!m_scheduler.restore(task, &error) && !error.isEmpty()) QMessageBox::warning(this, QStringLiteral("恢复失败"), error);
    } else {
        SettingsStore::clearTask();
    }
}

QString MainWindow::formatDuration(qint64 seconds) const {
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    return QStringLiteral("%1:%2:%3").arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

void MainWindow::scheduleAt() {
    QString error;
    if (!m_scheduler.scheduleAt(m_dateTimeEdit->dateTime(), m_force->isChecked(), m_fallback->isChecked(), &error)) QMessageBox::warning(this, QStringLiteral("设置失败"), error);
}

void MainWindow::scheduleCountdown() {
    const qint64 seconds = m_hours->value() * 3600LL + m_minutes->value() * 60LL + m_seconds->value();
    QString error;
    if (!m_scheduler.scheduleCountdown(seconds, m_force->isChecked(), m_fallback->isChecked(), &error)) QMessageBox::warning(this, QStringLiteral("设置失败"), error);
}

void MainWindow::cancelTask() { m_scheduler.cancel(); }

void MainWindow::togglePause() {
    if (m_scheduler.state() == ShutdownScheduler::State::Paused) m_scheduler.resume(); else m_scheduler.pause();
}

void MainWindow::showFromTray() { showNormal(); raise(); activateWindow(); }

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showFromTray();
}

void MainWindow::updateRemaining(qint64 seconds) { m_remaining->setText(seconds > 0 ? formatDuration(seconds) : QStringLiteral("--")); }

void MainWindow::updateState(ShutdownScheduler::State state) {
    QString text;
    switch (state) {
    case ShutdownScheduler::State::Idle: text = QStringLiteral("空闲"); break;
    case ShutdownScheduler::State::Armed: text = QStringLiteral("已设置"); break;
    case ShutdownScheduler::State::Paused: text = QStringLiteral("已暂停"); break;
    case ShutdownScheduler::State::Executing: text = QStringLiteral("正在关机"); break;
    case ShutdownScheduler::State::Completed: text = QStringLiteral("已完成"); break;
    case ShutdownScheduler::State::Error: text = QStringLiteral("失败"); break;
    }
    m_status->setText(text);
    m_pauseButton->setEnabled(m_scheduler.isActive());
    m_pauseButton->setText(state == ShutdownScheduler::State::Paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    if (m_cancelAction) m_cancelAction->setEnabled(m_scheduler.isActive());
}

void MainWindow::executeNow() {
    if (QMessageBox::warning(this, QStringLiteral("确认关机"), QStringLiteral("立即关机？"), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    m_scheduler.cancel();
    QString error;
    if (!ShutdownExecutor::execute(m_force->isChecked(), &error)) QMessageBox::critical(this, QStringLiteral("关机失败"), error);
}

void MainWindow::checkForUpdates() {
    m_silentUpdateCheck = false;
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(false);
        m_checkUpdateButton->setText(QStringLiteral("检查中..."));
    }
    m_updateManager.checkForUpdates();
}

void MainWindow::handleUpdateAvailable(const UpdateInfo &info) {
    m_availableUpdate = info;
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
        m_checkUpdateButton->setText(QStringLiteral("检查更新"));
    }
    const QString title = info.title.isEmpty() ? QStringLiteral("发现新版本") : info.title;
    const QString message = QStringLiteral("发现新版本 %1，当前版本 %2。\n\n是否立即下载？")
        .arg(info.version, QApplication::applicationVersion());
    m_silentUpdateCheck = false;
    if (QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        m_updateProgress->setRange(0, 100);
        m_updateProgress->setValue(0);
        m_updateProgress->setFormat(QStringLiteral("准备下载 %1").arg(info.assetName));
        m_updateManager.downloadUpdate(info);
    }
}

void MainWindow::handleNoUpdateAvailable() {
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
        m_checkUpdateButton->setText(QStringLiteral("检查更新"));
    }
    if (!m_silentUpdateCheck) QMessageBox::information(this, QStringLiteral("检查更新"), QStringLiteral("当前已经是最新版本。"));
    m_silentUpdateCheck = false;
}

void MainWindow::handleUpdateCheckError(const QString &message) {
    if (m_checkUpdateButton) {
        m_checkUpdateButton->setEnabled(true);
        m_checkUpdateButton->setText(QStringLiteral("检查更新"));
    }
    if (!m_silentUpdateCheck) QMessageBox::warning(this, QStringLiteral("检查更新失败"), message);
    m_silentUpdateCheck = false;
}

void MainWindow::handleDownloadProgress(qint64 received, qint64 total) {
    if (!m_updateProgress) return;
    if (total > 0) {
        m_updateProgress->setRange(0, 100);
        m_updateProgress->setValue(static_cast<int>((received * 100) / total));
        m_updateProgress->setFormat(QStringLiteral("下载中 %1 / %2 MB")
            .arg(received / 1024.0 / 1024.0, 0, 'f', 1)
            .arg(total / 1024.0 / 1024.0, 0, 'f', 1));
    } else {
        m_updateProgress->setRange(0, 0);
        m_updateProgress->setFormat(QStringLiteral("下载中..."));
    }
}

void MainWindow::handleDownloadFinished(const QString &filePath) {
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setValue(100);
    m_updateProgress->setFormat(QStringLiteral("下载完成"));
    if (QMessageBox::question(this, QStringLiteral("安装更新"), QStringLiteral("更新包已下载，立即重启安装？"), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!m_updateManager.installAndRestart(filePath, &error)) {
        QMessageBox::critical(this, QStringLiteral("安装更新失败"), error);
        return;
    }
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::handleDownloadError(const QString &message) {
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setValue(0);
    m_updateProgress->setFormat(QStringLiteral("下载失败"));
    QMessageBox::warning(this, QStringLiteral("下载更新失败"), message);
}

bool MainWindow::askCloseWithActiveTask() {
    const auto choice = QMessageBox::question(this, QStringLiteral("退出程序"), QStringLiteral("当前存在活动任务。退出时保留任务吗？"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::No) m_scheduler.cancel();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_scheduler.isActive() && !askCloseWithActiveTask()) { event->ignore(); return; }
    if (m_tray && m_tray->isVisible() && !m_forceQuit) { hide(); event->ignore(); return; }
    event->accept();
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized() && m_tray) hide();
}
