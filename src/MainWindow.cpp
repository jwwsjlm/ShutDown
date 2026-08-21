#include "MainWindow.h"

#include "AppLogger.h"
#include "SettingsStore.h"
#include "ShutdownExecutor.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace {
enum : int {
    IDC_DATE = 1001, IDC_TIME, IDC_HOURS, IDC_MINUTES, IDC_SECONDS, IDC_FORCE, IDC_FALLBACK,
    IDC_AT, IDC_COUNTDOWN, IDC_PAUSE, IDC_CANCEL, IDC_NOW, IDC_CHECK, IDC_PROGRESS, IDC_SETTINGS, IDC_SETTINGS_BACK,
    IDC_STATUS, IDC_REMAINING, ID_TRAY_SHOW = 2001, ID_TRAY_CANCEL, ID_TRAY_CHECK,
    ID_TRAY_NOW, ID_TRAY_EXIT
};
constexpr UINT WM_TRAY = WM_APP + 10;
constexpr UINT WM_UI_EVENT = WM_APP + 11;
constexpr UINT TIMER_SCHEDULER = 1;

HWND control(DWORD exStyle, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, int id) {
    return CreateWindowExW(exStyle, cls, title, style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void setFont(HWND hwnd, HFONT font) { SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }

bool isChecked(HWND hwnd) { return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED; }

std::time_t pickerDateTime(HWND datePicker, HWND timePicker) {
    SYSTEMTIME st{};
    SYSTEMTIME time{};
    if (DateTime_GetSystemtime(datePicker, &st) != GDT_VALID ||
        DateTime_GetSystemtime(timePicker, &time) != GDT_VALID) return 0;
    st.wHour = time.wHour;
    st.wMinute = time.wMinute;
    st.wSecond = time.wSecond;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    ULARGE_INTEGER value64{}; value64.LowPart = ft.dwLowDateTime; value64.HighPart = ft.dwHighDateTime;
    return static_cast<std::time_t>(value64.QuadPart / 10000000ULL - 11644473600ULL);
}

void setPickerDateTime(HWND datePicker, HWND timeEdit, std::time_t target) {
    std::tm local{}; localtime_s(&local, &target);
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(local.tm_year + 1900);
    st.wMonth = static_cast<WORD>(local.tm_mon + 1);
    st.wDay = static_cast<WORD>(local.tm_mday);
    DateTime_SetSystemtime(datePicker, GDT_VALID, &st);
    SYSTEMTIME time{};
    time.wYear = st.wYear; time.wMonth = st.wMonth; time.wDay = st.wDay;
    time.wHour = static_cast<WORD>(local.tm_hour);
    time.wMinute = static_cast<WORD>(local.tm_min);
    time.wSecond = static_cast<WORD>(local.tm_sec);
    DateTime_SetSystemtime(timeEdit, GDT_VALID, &time);
}
}

MainWindow::MainWindow(std::string version) : m_updateManager(std::move(version)) {
    m_scheduler.setStateCallback([this](ShutdownScheduler::State state) { updateState(state); });
    m_scheduler.setRemainingCallback([this](std::int64_t seconds) { updateRemaining(seconds); });
    m_scheduler.setErrorCallback([this](const std::wstring &message) { ::MessageBoxW(GetHwnd(), message.c_str(), L"关机失败", MB_ICONERROR); });
    UpdateManager::Callbacks callbacks;
    callbacks.updateAvailable = [this](const UpdateInfo &info) { auto *event = new UiEvent{}; event->type = UiEvent::Type::UpdateAvailable; event->info = info; post(event); };
    callbacks.noUpdateAvailable = [this] { auto *event = new UiEvent{}; event->type = UiEvent::Type::NoUpdate; post(event); };
    callbacks.checkError = [this](const std::wstring &text) { auto *event = new UiEvent{}; event->type = UiEvent::Type::CheckError; event->text = text; post(event); };
    callbacks.downloadProgress = [this](std::int64_t r, std::int64_t t) { auto *event = new UiEvent{}; event->type = UiEvent::Type::DownloadProgress; event->received = r; event->total = t; post(event); };
    callbacks.downloadFinished = [this](const std::wstring &path) { auto *event = new UiEvent{}; event->type = UiEvent::Type::DownloadFinished; event->text = path; post(event); };
    callbacks.downloadError = [this](const std::wstring &text) { auto *event = new UiEvent{}; event->type = UiEvent::Type::DownloadError; event->text = text; post(event); };
    m_updateManager.setCallbacks(std::move(callbacks));
}

MainWindow::~MainWindow() { destroyTray(); if (m_font) DeleteObject(m_font); }

void MainWindow::PreRegisterClass(WNDCLASS &wc) {
    wc.lpszClassName = L"ShutDown.Win32xx.MainWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
}

void MainWindow::PreCreate(CREATESTRUCT &cs) {
    // Win32++ removes WS_VISIBLE before CreateWindowEx and restores it only
    // when it is present in CREATESTRUCT. Without it the first launch stayed
    // hidden in the tray.
    cs.style = WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    cs.x = CW_USEDEFAULT; cs.y = CW_USEDEFAULT; cs.cx = 520; cs.cy = 370;
    cs.lpszName = L"定时关机";
}

HWND MainWindow::CreateMain() {
    const HWND hwnd = Create();
    if (hwnd) {
        ::ShowWindow(hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(hwnd);
    }
    return hwnd;
}

int MainWindow::OnCreate(CREATESTRUCT &) {
    m_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    createControls(); createTray(); restorePersistedTask();
    ::SetTimer(GetHwnd(), TIMER_SCHEDULER, 1000, nullptr);
    return 0;
}

void MainWindow::createControls() {
    const int left = 18, width = 465;
    auto *groupAt = control(0, L"BUTTON", L"指定日期和时间", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 8, width, 88, GetHwnd(), 0);
    auto *labelAt = control(0, L"STATIC", L"关机时间:", WS_CHILD | WS_VISIBLE, 32, 40, 80, 24, GetHwnd(), 0);
    m_dateEdit = control(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATECENTURYFORMAT, 115, 36, 150, 28, GetHwnd(), IDC_DATE);
    m_timeEdit = control(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT | DTS_UPDOWN, 275, 36, 95, 28, GetHwnd(), IDC_TIME);
    DateTime_SetFormat(m_timeEdit, L"HH:mm:ss");
    setPickerDateTime(m_dateEdit, m_timeEdit, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 3600);
    auto *atButton = control(0, L"BUTTON", L"设置定时关机", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 380, 35, 100, 30, GetHwnd(), IDC_AT);
    auto *groupCount = control(0, L"BUTTON", L"倒计时关机", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 103, width, 88, GetHwnd(), 0);
    control(0, L"STATIC", L"时长:", WS_CHILD | WS_VISIBLE, 32, 136, 50, 24, GetHwnd(), 0);
    m_hours = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 85, 132, 48, 26, GetHwnd(), IDC_HOURS);
    m_minutes = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 155, 132, 48, 26, GetHwnd(), IDC_MINUTES);
    m_seconds = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 225, 132, 48, 26, GetHwnd(), IDC_SECONDS);
    control(0, L"STATIC", L"时", WS_CHILD | WS_VISIBLE, 136, 136, 20, 24, GetHwnd(), 0);
    control(0, L"STATIC", L"分", WS_CHILD | WS_VISIBLE, 206, 136, 20, 24, GetHwnd(), 0);
    control(0, L"STATIC", L"秒", WS_CHILD | WS_VISIBLE, 276, 136, 20, 24, GetHwnd(), 0);
    auto *countButton = control(0, L"BUTTON", L"开始倒计时", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 380, 131, 100, 30, GetHwnd(), IDC_COUNTDOWN);
    auto *groupStatus = control(0, L"BUTTON", L"当前任务", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 198, width, 68, GetHwnd(), 0);
    control(0, L"STATIC", L"状态:", WS_CHILD | WS_VISIBLE, 32, 224, 50, 24, GetHwnd(), 0);
    m_status = control(0, L"STATIC", L"空闲", WS_CHILD | WS_VISIBLE, 85, 224, 130, 24, GetHwnd(), IDC_STATUS);
    control(0, L"STATIC", L"剩余:", WS_CHILD | WS_VISIBLE, 270, 224, 50, 24, GetHwnd(), 0);
    m_remaining = control(0, L"STATIC", L"--", WS_CHILD | WS_VISIBLE, 325, 224, 140, 24, GetHwnd(), IDC_REMAINING);
    m_pause = control(0, L"BUTTON", L"暂停", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 32, 282, 95, 30, GetHwnd(), IDC_PAUSE);
    control(0, L"BUTTON", L"取消任务", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 140, 282, 95, 30, GetHwnd(), IDC_CANCEL);
    control(0, L"BUTTON", L"立即关机", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 248, 282, 95, 30, GetHwnd(), IDC_NOW);
    m_settings = control(0, L"BUTTON", L"设置", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 356, 282, 124, 30, GetHwnd(), IDC_SETTINGS);
    m_settingsGroup = control(0, L"BUTTON", L"设置", BS_GROUPBOX | WS_CHILD, left, 8, width, 305, GetHwnd(), 0);
    m_force = control(0, L"BUTTON", L"强制关闭应用（可能丢失未保存数据）", WS_CHILD | BS_AUTOCHECKBOX, 35, 55, 420, 24, GetHwnd(), IDC_FORCE);
    m_fallback = control(0, L"BUTTON", L"启用 Task Scheduler 系统兜底", WS_CHILD | BS_AUTOCHECKBOX, 35, 88, 380, 24, GetHwnd(), IDC_FALLBACK);
    m_checkUpdate = control(0, L"BUTTON", L"检查更新", WS_CHILD | BS_PUSHBUTTON, 35, 135, 120, 30, GetHwnd(), IDC_CHECK);
    m_progress = control(0, PROGRESS_CLASSW, L"", WS_CHILD, 170, 138, 295, 24, GetHwnd(), IDC_PROGRESS);
    m_settingsBack = control(0, L"BUTTON", L"返回主界面", WS_CHILD | BS_PUSHBUTTON, 35, 250, 120, 30, GetHwnd(), IDC_SETTINGS_BACK);
    SendMessageW(m_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    for (HWND child : {groupAt, labelAt, m_dateEdit, m_timeEdit, atButton, groupCount, m_hours, m_minutes, m_seconds, countButton, groupStatus, m_status, m_remaining, m_pause, m_settings, m_settingsGroup, m_force, m_fallback, m_checkUpdate, m_progress, m_settingsBack}) setFont(child, m_font);
    EnumChildWindows(GetHwnd(), [](HWND hwnd, LPARAM font) { setFont(hwnd, reinterpret_cast<HFONT>(font)); return TRUE; }, reinterpret_cast<LPARAM>(m_font));
    setSettingsVisible(false);
}

void MainWindow::setSettingsVisible(bool visible) {
    const int cmd = visible ? SW_HIDE : SW_SHOW;
    for (HWND hwnd : {m_dateEdit, m_timeEdit, m_hours, m_minutes, m_seconds, m_pause, m_settings, m_status, m_remaining}) if (hwnd) ::ShowWindow(hwnd, cmd);
    // Static/group controls are found by fixed IDs only for interactive controls;
    // hide the main surface by toggling the parent children except settings controls.
    for (HWND hwnd = ::GetWindow(GetHwnd(), GW_CHILD); hwnd; hwnd = ::GetWindow(hwnd, GW_HWNDNEXT)) {
        const int id = static_cast<int>(::GetDlgCtrlID(hwnd));
        const bool isSettings = hwnd == m_settingsGroup || hwnd == m_force || hwnd == m_fallback || hwnd == m_checkUpdate || hwnd == m_progress || hwnd == m_settingsBack;
        if (isSettings) ::ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
        else if (id == 0 || id == IDC_AT || id == IDC_COUNTDOWN || id == IDC_CANCEL || id == IDC_NOW) ::ShowWindow(hwnd, visible ? SW_HIDE : SW_SHOW);
    }
}

void MainWindow::createTray() {
    m_tray = NOTIFYICONDATAW{}; m_tray.cbSize = sizeof(NOTIFYICONDATAW); m_tray.hWnd = GetHwnd(); m_tray.uID = 1; m_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; m_tray.uCallbackMessage = WM_TRAY; m_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(m_tray.szTip, L"定时关机");
    m_trayCreated = Shell_NotifyIconW(NIM_ADD, &m_tray) == TRUE;
}

void MainWindow::destroyTray() { if (m_trayCreated) Shell_NotifyIconW(NIM_DELETE, &m_tray); m_trayCreated = false; }

void MainWindow::restorePersistedTask() {
    if (!SettingsStore::hasTask()) return;
    if (::MessageBoxW(GetHwnd(), L"检测到上次未完成的关机任务，是否恢复？", L"恢复任务", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::wstring error; if (!m_scheduler.restore(SettingsStore::loadTask(), &error) && !error.empty()) ::MessageBoxW(GetHwnd(), error.c_str(), L"恢复失败", MB_OK | MB_ICONWARNING);
    } else SettingsStore::clearTask();
}

void MainWindow::setText(HWND controlHandle, const std::wstring &value) { ::SetWindowTextW(controlHandle, value.c_str()); }
std::wstring MainWindow::text(HWND controlHandle) const { wchar_t buffer[512]{}; ::GetWindowTextW(controlHandle, buffer, 512); return buffer; }

void MainWindow::scheduleAt() {
    std::wstring error;
    const auto target = pickerDateTime(m_dateEdit, m_timeEdit);
    if (!m_scheduler.scheduleAt(target, isChecked(m_force), isChecked(m_fallback), &error)) ::MessageBoxW(GetHwnd(), error.c_str(), L"设置失败", MB_OK | MB_ICONWARNING);
}

void MainWindow::scheduleCountdown() {
    const auto h = _wtoi(text(m_hours).c_str()), m = _wtoi(text(m_minutes).c_str()), s = _wtoi(text(m_seconds).c_str());
    const std::int64_t seconds = static_cast<std::int64_t>(h) * 3600 + m * 60 + s;
    if (seconds <= 0) {
        ::MessageBoxW(GetHwnd(), L"倒计时必须大于 0 秒。", L"设置失败", MB_OK | MB_ICONWARNING);
        return;
    }
    if (seconds <= 10 * 60) {
        const int answer = ::MessageBoxW(
            GetHwnd(),
            L"当前倒计时距离关机只有 10 分钟以内，确认输入的日期/倒计时无误吗？",
            L"确认定时关机",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (answer != IDYES) return;
    }
    std::wstring error;
    if (!m_scheduler.scheduleCountdown(seconds, isChecked(m_force), isChecked(m_fallback), &error)) {
        ::MessageBoxW(GetHwnd(), error.c_str(), L"设置失败", MB_OK | MB_ICONWARNING);
        return;
    }
    // 倒计时启动后，把预计关机时刻同步显示到日期选择框和时间框，方便核对。
    const auto target = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + seconds;
    setPickerDateTime(m_dateEdit, m_timeEdit, target);
}

void MainWindow::cancelTask() { m_scheduler.cancel(); }
void MainWindow::togglePause() { m_scheduler.state() == ShutdownScheduler::State::Paused ? m_scheduler.resume() : m_scheduler.pause(); }

void MainWindow::executeNow() {
    if (::MessageBoxW(GetHwnd(), L"立即关机？", L"确认关机", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    m_scheduler.cancel(); std::wstring error; if (!ShutdownExecutor::execute(isChecked(m_force), &error)) ::MessageBoxW(GetHwnd(), error.c_str(), L"关机失败", MB_OK | MB_ICONERROR);
}

void MainWindow::checkForUpdates(bool silent) {
    m_silentUpdateCheck = silent; ::EnableWindow(m_checkUpdate, FALSE); setText(m_checkUpdate, L"检查中..."); m_updateManager.checkForUpdates();
}

std::wstring MainWindow::formatDuration(std::int64_t seconds) {
    wchar_t buffer[64]{}; swprintf_s(buffer, 64, L"%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60); return buffer;
}

void MainWindow::updateRemaining(std::int64_t seconds) { setText(m_remaining, seconds > 0 ? formatDuration(seconds) : L"--"); }

void MainWindow::updateState(ShutdownScheduler::State state) {
    const wchar_t *label = L"空闲"; if (state == ShutdownScheduler::State::Armed) label = L"已设置"; else if (state == ShutdownScheduler::State::Paused) label = L"已暂停"; else if (state == ShutdownScheduler::State::Executing) label = L"正在关机"; else if (state == ShutdownScheduler::State::Completed) label = L"已完成"; else if (state == ShutdownScheduler::State::Error) label = L"失败";
    setText(m_status, label); ::EnableWindow(m_pause, m_scheduler.isActive()); setText(m_pause, state == ShutdownScheduler::State::Paused ? L"继续" : L"暂停");
}

void MainWindow::showFromTray() { ::ShowWindow(GetHwnd(), SW_SHOWNORMAL); ::SetForegroundWindow(GetHwnd()); }

void MainWindow::post(UiEvent *event) { if (!PostMessageW(GetHwnd(), WM_UI_EVENT, 0, reinterpret_cast<LPARAM>(event))) delete event; }

void MainWindow::handleEvent(std::unique_ptr<UiEvent> event) {
    switch (event->type) {
    case UiEvent::Type::UpdateAvailable:
        m_availableUpdate = event->info; ::EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); m_silentUpdateCheck = false;
        if (::MessageBoxW(GetHwnd(), (L"发现新版本 " + std::wstring(event->info.version.begin(), event->info.version.end()) + L"，是否立即下载？").c_str(), L"发现新版本", MB_YESNO | MB_ICONINFORMATION) == IDYES) { SendMessageW(m_progress, PBM_SETPOS, 0, 0); m_updateManager.downloadUpdate(event->info); }
        break;
    case UiEvent::Type::NoUpdate: ::EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); if (!m_silentUpdateCheck) ::MessageBoxW(GetHwnd(), L"当前已经是最新版本。", L"检查更新", MB_OK | MB_ICONINFORMATION); m_silentUpdateCheck = false; break;
    case UiEvent::Type::CheckError: ::EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); if (!m_silentUpdateCheck) ::MessageBoxW(GetHwnd(), event->text.c_str(), L"检查更新失败", MB_OK | MB_ICONWARNING); m_silentUpdateCheck = false; break;
    case UiEvent::Type::DownloadProgress: if (event->total > 0) SendMessageW(m_progress, PBM_SETPOS, static_cast<WPARAM>(event->received * 100 / event->total), 0); break;
    case UiEvent::Type::DownloadFinished:
        SendMessageW(m_progress, PBM_SETPOS, 100, 0);
        if (::MessageBoxW(GetHwnd(), L"更新包已下载，立即重启安装？", L"安装更新", MB_YESNO | MB_ICONQUESTION) == IDYES) { std::wstring error; if (!m_updateManager.installAndRestart(event->text, &error)) ::MessageBoxW(GetHwnd(), error.c_str(), L"安装更新失败", MB_OK | MB_ICONERROR); else { m_forceQuit = true; DestroyWindow(GetHwnd()); } }
        break;
    case UiEvent::Type::DownloadError: SendMessageW(m_progress, PBM_SETPOS, 0, 0); ::MessageBoxW(GetHwnd(), event->text.c_str(), L"下载更新失败", MB_OK | MB_ICONWARNING); break;
    }
}

BOOL MainWindow::OnCommand(WPARAM wparam, LPARAM) {
    switch (LOWORD(wparam)) { case IDC_AT: scheduleAt(); return TRUE; case IDC_COUNTDOWN: scheduleCountdown(); return TRUE; case IDC_PAUSE: togglePause(); return TRUE; case IDC_CANCEL: cancelTask(); return TRUE; case IDC_NOW: executeNow(); return TRUE; case IDC_SETTINGS: setSettingsVisible(true); return TRUE; case IDC_SETTINGS_BACK: setSettingsVisible(false); return TRUE; case IDC_CHECK: checkForUpdates(); return TRUE; }
    return FALSE;
}

bool MainWindow::askCloseWithActiveTask() {
    const int choice = ::MessageBoxW(GetHwnd(), L"当前存在活动任务。退出时保留任务吗？", L"退出程序", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) m_scheduler.cancel();
    return true;
}

void MainWindow::OnClose() { if (!m_forceQuit && m_scheduler.isActive() && !askCloseWithActiveTask()) return; DestroyWindow(GetHwnd()); }
void MainWindow::OnDestroy() { destroyTray(); PostQuitMessage(0); }

LRESULT MainWindow::WndProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_TIMER) { if (wparam == TIMER_SCHEDULER) m_scheduler.tick(); return 0; }
    if (msg == WM_SIZE && wparam == SIZE_MINIMIZED) { ::ShowWindow(GetHwnd(), SW_HIDE); return 0; }
    if (msg == WM_TRAY && m_trayCreated) { if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) showFromTray(); if (lparam == WM_RBUTTONUP) { POINT point{}; GetCursorPos(&point); HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示窗口"); AppendMenuW(menu, MF_STRING, ID_TRAY_CANCEL, L"取消任务"); AppendMenuW(menu, MF_STRING, ID_TRAY_CHECK, L"检查更新"); AppendMenuW(menu, MF_STRING, ID_TRAY_NOW, L"立即关机"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出"); ::SetForegroundWindow(GetHwnd()); const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, GetHwnd(), nullptr); DestroyMenu(menu); if (cmd == ID_TRAY_SHOW) showFromTray(); else if (cmd == ID_TRAY_CANCEL) cancelTask(); else if (cmd == ID_TRAY_CHECK) checkForUpdates(); else if (cmd == ID_TRAY_NOW) executeNow(); else if (cmd == ID_TRAY_EXIT) { m_forceQuit = true; DestroyWindow(GetHwnd()); } } return 0; }
    if (msg == WM_UI_EVENT) { handleEvent(std::unique_ptr<UiEvent>(reinterpret_cast<UiEvent *>(lparam))); return 0; }
    return CWnd::WndProc(msg, wparam, lparam);
}
