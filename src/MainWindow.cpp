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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
enum : int {
    IDC_DATE = 1001, IDC_HOURS, IDC_MINUTES, IDC_SECONDS, IDC_FORCE, IDC_FALLBACK,
    IDC_AT, IDC_COUNTDOWN, IDC_PAUSE, IDC_CANCEL, IDC_NOW, IDC_CHECK, IDC_PROGRESS,
    IDC_STATUS, IDC_REMAINING, ID_TRAY_SHOW = 2001, ID_TRAY_CANCEL, ID_TRAY_CHECK,
    ID_TRAY_NOW, ID_TRAY_EXIT
};
constexpr UINT WM_TRAY = WM_APP + 10;
constexpr UINT WM_UI_EVENT = WM_APP + 11;
constexpr UINT TIMER_SCHEDULER = 1;
constexpr UINT TIMER_AUTO_UPDATE = 2;

HWND control(DWORD exStyle, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, int id) {
    return CreateWindowExW(exStyle, cls, title, style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void setFont(HWND hwnd, HFONT font) { SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }

std::wstring numberText(HWND hwnd) {
    wchar_t buffer[64]{}; GetWindowTextW(hwnd, buffer, 64); return buffer;
}

std::time_t parseDate(const std::wstring &value) {
    SYSTEMTIME st{}; int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (swscanf_s(value.c_str(), L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return 0;
    st.wYear = static_cast<WORD>(year); st.wMonth = static_cast<WORD>(month); st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour); st.wMinute = static_cast<WORD>(minute); st.wSecond = static_cast<WORD>(second);
    FILETIME ft{}; if (!SystemTimeToFileTime(&st, &ft)) return 0;
    ULARGE_INTEGER value64{}; value64.LowPart = ft.dwLowDateTime; value64.HighPart = ft.dwHighDateTime;
    return static_cast<std::time_t>(value64.QuadPart / 10000000ULL - 11644473600ULL);
}

std::wstring currentPlusHour() {
    const auto target = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 3600;
    std::tm local{}; localtime_s(&local, &target); wchar_t buffer[64]{};
    swprintf_s(buffer, 64, L"%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}
}

MainWindow::MainWindow(std::string version) : m_updateManager(std::move(version)) {
    m_scheduler.setStateCallback([this](ShutdownScheduler::State state) { updateState(state); });
    m_scheduler.setRemainingCallback([this](std::int64_t seconds) { updateRemaining(seconds); });
    m_scheduler.setErrorCallback([this](const std::wstring &message) { MessageBoxW(GetHwnd(), message.c_str(), L"关机失败", MB_ICONERROR); });
    UpdateManager::Callbacks callbacks;
    callbacks.updateAvailable = [this](const UpdateInfo &info) { auto *event = new UiEvent{UiEvent::Type::UpdateAvailable}; event->info = info; post(event); };
    callbacks.noUpdateAvailable = [this] { post(new UiEvent{UiEvent::Type::NoUpdate}); };
    callbacks.checkError = [this](const std::wstring &text) { auto *event = new UiEvent{UiEvent::Type::CheckError}; event->text = text; post(event); };
    callbacks.downloadProgress = [this](std::int64_t r, std::int64_t t) { auto *event = new UiEvent{UiEvent::Type::DownloadProgress}; event->received = r; event->total = t; post(event); };
    callbacks.downloadFinished = [this](const std::wstring &path) { auto *event = new UiEvent{UiEvent::Type::DownloadFinished}; event->text = path; post(event); };
    callbacks.downloadError = [this](const std::wstring &text) { auto *event = new UiEvent{UiEvent::Type::DownloadError}; event->text = text; post(event); };
    m_updateManager.setCallbacks(std::move(callbacks));
}

MainWindow::~MainWindow() { destroyTray(); if (m_font) DeleteObject(m_font); }

void MainWindow::PreRegisterClass(WNDCLASS &wc) {
    wc.lpszClassName = L"ShutDown.Win32xx.MainWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
}

void MainWindow::PreCreate(CREATESTRUCT &cs) {
    cs.style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    cs.x = CW_USEDEFAULT; cs.y = CW_USEDEFAULT; cs.cx = 520; cs.cy = 560;
    cs.lpszName = L"定时关机";
}

HWND MainWindow::CreateMain() { return Create(); }

int MainWindow::OnCreate(CREATESTRUCT &) {
    m_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    createControls(); createTray(); restorePersistedTask();
    SetTimer(GetHwnd(), TIMER_SCHEDULER, 1000, nullptr);
    SetTimer(GetHwnd(), TIMER_AUTO_UPDATE, 3000, nullptr);
    return 0;
}

void MainWindow::createControls() {
    const int left = 18, width = 480;
    auto *groupAt = control(0, L"BUTTON", L"指定日期和时间", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 10, width, 95, GetHwnd(), 0);
    auto *labelAt = control(0, L"STATIC", L"关机时间:", WS_CHILD | WS_VISIBLE, 35, 40, 80, 24, GetHwnd(), 0);
    m_dateEdit = control(WS_EX_CLIENTEDGE, L"EDIT", currentPlusHour().c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 37, 210, 28, GetHwnd(), IDC_DATE);
    auto *atButton = control(0, L"BUTTON", L"设置定时关机", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 340, 36, 130, 30, GetHwnd(), IDC_AT);
    auto *groupCount = control(0, L"BUTTON", L"倒计时关机", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 115, width, 100, GetHwnd(), 0);
    control(0, L"STATIC", L"时长:", WS_CHILD | WS_VISIBLE, 35, 145, 50, 24, GetHwnd(), 0);
    m_hours = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 90, 142, 55, 28, GetHwnd(), IDC_HOURS);
    m_minutes = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 170, 142, 55, 28, GetHwnd(), IDC_MINUTES);
    m_seconds = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 250, 142, 55, 28, GetHwnd(), IDC_SECONDS);
    control(0, L"STATIC", L"时", WS_CHILD | WS_VISIBLE, 148, 145, 20, 24, GetHwnd(), 0);
    control(0, L"STATIC", L"分", WS_CHILD | WS_VISIBLE, 228, 145, 20, 24, GetHwnd(), 0);
    control(0, L"STATIC", L"秒", WS_CHILD | WS_VISIBLE, 308, 145, 20, 24, GetHwnd(), 0);
    auto *countButton = control(0, L"BUTTON", L"开始倒计时", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 340, 141, 130, 30, GetHwnd(), IDC_COUNTDOWN);
    auto *groupOptions = control(0, L"BUTTON", L"选项", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 225, width, 75, GetHwnd(), 0);
    m_force = control(0, L"BUTTON", L"强制关闭应用（可能丢失未保存数据）", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 248, 300, 24, GetHwnd(), IDC_FORCE);
    m_fallback = control(0, L"BUTTON", L"启用 Task Scheduler 系统兜底", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 35, 273, 260, 24, GetHwnd(), IDC_FALLBACK);
    auto *groupStatus = control(0, L"BUTTON", L"当前任务", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, left, 310, width, 75, GetHwnd(), 0);
    control(0, L"STATIC", L"状态:", WS_CHILD | WS_VISIBLE, 35, 333, 60, 24, GetHwnd(), 0);
    m_status = control(0, L"STATIC", L"空闲", WS_CHILD | WS_VISIBLE, 100, 333, 160, 24, GetHwnd(), IDC_STATUS);
    control(0, L"STATIC", L"剩余时间:", WS_CHILD | WS_VISIBLE, 280, 333, 80, 24, GetHwnd(), 0);
    m_remaining = control(0, L"STATIC", L"--", WS_CHILD | WS_VISIBLE, 365, 333, 100, 24, GetHwnd(), IDC_REMAINING);
    m_pause = control(0, L"BUTTON", L"暂停", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 35, 400, 105, 32, GetHwnd(), IDC_PAUSE);
    control(0, L"BUTTON", L"取消任务", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 155, 400, 105, 32, GetHwnd(), IDC_CANCEL);
    control(0, L"BUTTON", L"立即关机", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 275, 400, 105, 32, GetHwnd(), IDC_NOW);
    m_checkUpdate = control(0, L"BUTTON", L"检查更新", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 35, 455, 120, 32, GetHwnd(), IDC_CHECK);
    m_progress = control(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 165, 457, 300, 28, GetHwnd(), IDC_PROGRESS);
    SendMessageW(m_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    for (HWND child : {groupAt, labelAt, m_dateEdit, atButton, groupCount, m_hours, m_minutes, m_seconds, countButton, groupOptions, m_force, m_fallback, groupStatus, m_status, m_remaining, m_pause, m_checkUpdate, m_progress}) setFont(child, m_font);
    EnumChildWindows(GetHwnd(), [](HWND hwnd, LPARAM font) { setFont(hwnd, reinterpret_cast<HFONT>(font)); return TRUE; }, reinterpret_cast<LPARAM>(m_font));
}

void MainWindow::createTray() {
    m_tray = {sizeof(NOTIFYICONDATAW)}; m_tray.hWnd = GetHwnd(); m_tray.uID = 1; m_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; m_tray.uCallbackMessage = WM_TRAY; m_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(m_tray.szTip, L"定时关机");
    m_trayCreated = Shell_NotifyIconW(NIM_ADD, &m_tray) == TRUE;
}

void MainWindow::destroyTray() { if (m_trayCreated) Shell_NotifyIconW(NIM_DELETE, &m_tray); m_trayCreated = false; }

void MainWindow::restorePersistedTask() {
    if (!SettingsStore::hasTask()) return;
    if (MessageBoxW(GetHwnd(), L"检测到上次未完成的关机任务，是否恢复？", L"恢复任务", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::wstring error; if (!m_scheduler.restore(SettingsStore::loadTask(), &error) && !error.empty()) MessageBoxW(GetHwnd(), error.c_str(), L"恢复失败", MB_OK | MB_ICONWARNING);
    } else SettingsStore::clearTask();
}

void MainWindow::setText(HWND controlHandle, const std::wstring &value) { SetWindowTextW(controlHandle, value.c_str()); }
std::wstring MainWindow::text(HWND controlHandle) const { wchar_t buffer[512]{}; GetWindowTextW(controlHandle, buffer, 512); return buffer; }

void MainWindow::scheduleAt() {
    std::wstring error; if (!m_scheduler.scheduleAt(parseDate(text(m_dateEdit)), Button_GetCheck(m_force) == BST_CHECKED, Button_GetCheck(m_fallback) == BST_CHECKED, &error)) MessageBoxW(GetHwnd(), error.c_str(), L"设置失败", MB_OK | MB_ICONWARNING);
}

void MainWindow::scheduleCountdown() {
    const auto h = _wtoi(text(m_hours).c_str()), m = _wtoi(text(m_minutes).c_str()), s = _wtoi(text(m_seconds).c_str());
    std::wstring error; if (!m_scheduler.scheduleCountdown(static_cast<std::int64_t>(h) * 3600 + m * 60 + s, Button_GetCheck(m_force) == BST_CHECKED, Button_GetCheck(m_fallback) == BST_CHECKED, &error)) MessageBoxW(GetHwnd(), error.c_str(), L"设置失败", MB_OK | MB_ICONWARNING);
}

void MainWindow::cancelTask() { m_scheduler.cancel(); }
void MainWindow::togglePause() { m_scheduler.state() == ShutdownScheduler::State::Paused ? m_scheduler.resume() : m_scheduler.pause(); }

void MainWindow::executeNow() {
    if (MessageBoxW(GetHwnd(), L"立即关机？", L"确认关机", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    m_scheduler.cancel(); std::wstring error; if (!ShutdownExecutor::execute(Button_GetCheck(m_force) == BST_CHECKED, &error)) MessageBoxW(GetHwnd(), error.c_str(), L"关机失败", MB_OK | MB_ICONERROR);
}

void MainWindow::checkForUpdates(bool silent) {
    m_silentUpdateCheck = silent; EnableWindow(m_checkUpdate, FALSE); setText(m_checkUpdate, L"检查中..."); m_updateManager.checkForUpdates();
}

std::wstring MainWindow::formatDuration(std::int64_t seconds) {
    wchar_t buffer[64]{}; swprintf_s(buffer, 64, L"%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60); return buffer;
}

void MainWindow::updateRemaining(std::int64_t seconds) { setText(m_remaining, seconds > 0 ? formatDuration(seconds) : L"--"); }

void MainWindow::updateState(ShutdownScheduler::State state) {
    const wchar_t *label = L"空闲"; if (state == ShutdownScheduler::State::Armed) label = L"已设置"; else if (state == ShutdownScheduler::State::Paused) label = L"已暂停"; else if (state == ShutdownScheduler::State::Executing) label = L"正在关机"; else if (state == ShutdownScheduler::State::Completed) label = L"已完成"; else if (state == ShutdownScheduler::State::Error) label = L"失败";
    setText(m_status, label); EnableWindow(m_pause, m_scheduler.isActive()); setText(m_pause, state == ShutdownScheduler::State::Paused ? L"继续" : L"暂停");
}

void MainWindow::showFromTray() { ShowWindow(GetHwnd(), SW_SHOWNORMAL); SetForegroundWindow(GetHwnd()); }

void MainWindow::post(UiEvent *event) { if (!PostMessageW(GetHwnd(), WM_UI_EVENT, 0, reinterpret_cast<LPARAM>(event))) delete event; }

void MainWindow::handleEvent(std::unique_ptr<UiEvent> event) {
    switch (event->type) {
    case UiEvent::Type::UpdateAvailable:
        m_availableUpdate = event->info; EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); m_silentUpdateCheck = false;
        if (MessageBoxW(GetHwnd(), (L"发现新版本 " + std::wstring(event->info.version.begin(), event->info.version.end()) + L"，是否立即下载？").c_str(), L"发现新版本", MB_YESNO | MB_ICONINFORMATION) == IDYES) { SendMessageW(m_progress, PBM_SETPOS, 0, 0); m_updateManager.downloadUpdate(event->info); }
        break;
    case UiEvent::Type::NoUpdate: EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); if (!m_silentUpdateCheck) MessageBoxW(GetHwnd(), L"当前已经是最新版本。", L"检查更新", MB_OK | MB_ICONINFORMATION); m_silentUpdateCheck = false; break;
    case UiEvent::Type::CheckError: EnableWindow(m_checkUpdate, TRUE); setText(m_checkUpdate, L"检查更新"); if (!m_silentUpdateCheck) MessageBoxW(GetHwnd(), event->text.c_str(), L"检查更新失败", MB_OK | MB_ICONWARNING); m_silentUpdateCheck = false; break;
    case UiEvent::Type::DownloadProgress: if (event->total > 0) SendMessageW(m_progress, PBM_SETPOS, static_cast<WPARAM>(event->received * 100 / event->total), 0); break;
    case UiEvent::Type::DownloadFinished:
        SendMessageW(m_progress, PBM_SETPOS, 100, 0);
        if (MessageBoxW(GetHwnd(), L"更新包已下载，立即重启安装？", L"安装更新", MB_YESNO | MB_ICONQUESTION) == IDYES) { std::wstring error; if (!m_updateManager.installAndRestart(event->text, &error)) MessageBoxW(GetHwnd(), error.c_str(), L"安装更新失败", MB_OK | MB_ICONERROR); else { m_forceQuit = true; DestroyWindow(GetHwnd()); } }
        break;
    case UiEvent::Type::DownloadError: SendMessageW(m_progress, PBM_SETPOS, 0, 0); MessageBoxW(GetHwnd(), event->text.c_str(), L"下载更新失败", MB_OK | MB_ICONWARNING); break;
    }
}

BOOL MainWindow::OnCommand(WPARAM wparam, LPARAM) {
    switch (LOWORD(wparam)) { case IDC_AT: scheduleAt(); return TRUE; case IDC_COUNTDOWN: scheduleCountdown(); return TRUE; case IDC_PAUSE: togglePause(); return TRUE; case IDC_CANCEL: cancelTask(); return TRUE; case IDC_NOW: executeNow(); return TRUE; case IDC_CHECK: checkForUpdates(); return TRUE; }
    return FALSE;
}

bool MainWindow::askCloseWithActiveTask() {
    const int choice = MessageBoxW(GetHwnd(), L"当前存在活动任务。退出时保留任务吗？", L"退出程序", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false; if (choice == IDNO) m_scheduler.cancel(); return true;
}

void MainWindow::OnClose() { if (!m_forceQuit && m_scheduler.isActive() && !askCloseWithActiveTask()) return; DestroyWindow(GetHwnd()); }
void MainWindow::OnDestroy() { destroyTray(); PostQuitMessage(0); }

LRESULT MainWindow::WndProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_TIMER) { if (wparam == TIMER_SCHEDULER) m_scheduler.tick(); else if (wparam == TIMER_AUTO_UPDATE) { KillTimer(GetHwnd(), TIMER_AUTO_UPDATE); checkForUpdates(true); } return 0; }
    if (msg == WM_SIZE && wparam == SIZE_MINIMIZED) { ShowWindow(GetHwnd(), SW_HIDE); return 0; }
    if (msg == WM_TRAY && m_trayCreated) { if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) showFromTray(); if (lparam == WM_RBUTTONUP) { POINT point{}; GetCursorPos(&point); HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示窗口"); AppendMenuW(menu, MF_STRING, ID_TRAY_CANCEL, L"取消任务"); AppendMenuW(menu, MF_STRING, ID_TRAY_CHECK, L"检查更新"); AppendMenuW(menu, MF_STRING, ID_TRAY_NOW, L"立即关机"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出"); SetForegroundWindow(GetHwnd()); const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, GetHwnd(), nullptr); DestroyMenu(menu); if (cmd == ID_TRAY_SHOW) showFromTray(); else if (cmd == ID_TRAY_CANCEL) cancelTask(); else if (cmd == ID_TRAY_CHECK) checkForUpdates(); else if (cmd == ID_TRAY_NOW) executeNow(); else if (cmd == ID_TRAY_EXIT) { m_forceQuit = true; DestroyWindow(GetHwnd()); } } return 0; }
    if (msg == WM_UI_EVENT) { handleEvent(std::unique_ptr<UiEvent>(reinterpret_cast<UiEvent *>(lparam))); return 0; }
    return CWnd::WndProc(msg, wparam, lparam);
}
