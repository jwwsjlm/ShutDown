#include "MainWindow.h"

#include "AppLogger.h"
#include "SettingsStore.h"
#include "ShutdownExecutor.h"
#include "../resources/resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windows.h>

#include <chrono>
#include <ctime>
#include <memory>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

namespace {
enum : int {
    IDC_DATE = 1001, IDC_TIME, IDC_HOURS, IDC_MINUTES, IDC_SECONDS, IDC_FORCE, IDC_FALLBACK,
    IDC_AT, IDC_COUNTDOWN, IDC_PAUSE, IDC_CANCEL, IDC_NOW, IDC_CHECK, IDC_GITHUB, IDC_TAB,
    IDC_STATUS, IDC_REMAINING, ID_TRAY_SHOW = 2001, ID_TRAY_CANCEL, ID_TRAY_CHECK,
    ID_TRAY_NOW, ID_TRAY_EXIT
};
constexpr UINT WM_TRAY = WM_APP + 10;
constexpr UINT WM_UI_EVENT = WM_APP + 11;
constexpr UINT TIMER_SCHEDULER = 1;
constexpr wchar_t kProjectUrl[] = L"https://github.com/jwwsjlm/ShutDown";
constexpr wchar_t kLatestReleaseUrl[] = L"https://github.com/jwwsjlm/ShutDown/releases/latest";
// Win10 1903 之前头文件里没有这个枚举值，直接写字面量，旧系统上调用只会返回错误码。
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr COLORREF kDarkBg = RGB(32, 32, 32);
constexpr COLORREF kDarkEditBg = RGB(43, 43, 43);
constexpr COLORREF kDarkText = RGB(230, 230, 230);
constexpr COLORREF kDarkAccent = RGB(76, 194, 255);
constexpr COLORREF kLightLink = RGB(0, 102, 204);

HWND control(DWORD exStyle, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, int id) {
    return CreateWindowExW(exStyle, cls, title, style, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void setFont(HWND hwnd, HFONT font) { SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); }

void makeStaticTransparent(HWND hwnd) {
    if (!hwnd) return;
    const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
}

bool isChecked(HWND hwnd) { return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED; }

HICON loadAppIcon(int size) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                         size, size, LR_SHARED));
}

void repaintWindow(HWND hwnd) {
    if (!hwnd) return;
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

std::wstring utf8ToWide(const std::string &value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring updatePromptText(const std::string &version) {
    return L"发现新版本 v" + utf8ToWide(version) + L"。\n\n是否打开 GitHub 下载页面？";
}

void openUrl(HWND owner, const wchar_t *url) {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        ::MessageBoxW(owner, L"无法打开浏览器，请稍后重试。", L"打开链接失败", MB_OK | MB_ICONWARNING);
    }
}

std::time_t pickerDateTime(HWND datePicker, HWND timePicker) {
    SYSTEMTIME st{};
    SYSTEMTIME time{};
    if (DateTime_GetSystemtime(datePicker, &st) != GDT_VALID ||
        DateTime_GetSystemtime(timePicker, &time) != GDT_VALID) return 0;
    // DateTime Picker 返回的是本地时间，不能用 SystemTimeToFileTime 直接转 epoch；
    // 否则会被当成 UTC，东八区会多出约 8 小时。
    std::tm local{};
    local.tm_year = st.wYear - 1900;
    local.tm_mon = st.wMonth - 1;
    local.tm_mday = st.wDay;
    local.tm_hour = time.wHour;
    local.tm_min = time.wMinute;
    local.tm_sec = time.wSecond;
    local.tm_isdst = -1;
    const auto result = std::mktime(&local);
    return result == static_cast<std::time_t>(-1) ? 0 : result;
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

MainWindow::MainWindow(std::string version)
    : m_updateManager(version), m_windowTitle(L"定时关机 v" + utf8ToWide(version)) {
    m_scheduler.setStateCallback([this](ShutdownScheduler::State state) { updateState(state); });
    m_scheduler.setRemainingCallback([this](std::int64_t seconds) { updateRemaining(seconds); });
    m_scheduler.setErrorCallback([this](const std::wstring &message) { ::MessageBoxW(GetHwnd(), message.c_str(), L"关机失败", MB_ICONERROR); });
    UpdateManager::Callbacks callbacks;
    callbacks.updateAvailable = [this](const std::string &version) { auto *event = new UiEvent{}; event->type = UiEvent::Type::UpdateAvailable; event->version = version; post(event); };
    callbacks.noUpdateAvailable = [this] { auto *event = new UiEvent{}; event->type = UiEvent::Type::NoUpdate; post(event); };
    callbacks.checkError = [this](const std::wstring &text) { auto *event = new UiEvent{}; event->type = UiEvent::Type::CheckError; event->text = text; post(event); };
    m_updateManager.setCallbacks(std::move(callbacks));
}

MainWindow::~MainWindow() {
    destroyTray();
    if (m_font) DeleteObject(m_font);
    if (m_fontLarge) DeleteObject(m_fontLarge);
    if (m_linkFont) DeleteObject(m_linkFont);
    if (m_bgBrush) DeleteObject(m_bgBrush);
    if (m_editBrush) DeleteObject(m_editBrush);
}

void MainWindow::PreRegisterClass(WNDCLASS &wc) {
    wc.lpszClassName = L"ShutDown.Win32xx.MainWindow";
    wc.hIcon = loadAppIcon(32);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
}

void MainWindow::PreCreate(CREATESTRUCT &cs) {
    // Win32++ removes WS_VISIBLE before CreateWindowEx and restores it only
    // when it is present in CREATESTRUCT. Without it the first launch stayed
    // hidden in the tray.
    cs.style = WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    cs.x = CW_USEDEFAULT; cs.y = CW_USEDEFAULT; cs.cx = 530; cs.cy = 345;
    cs.lpszName = m_windowTitle.c_str();
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
    SendMessageW(GetHwnd(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(loadAppIcon(32)));
    SendMessageW(GetHwnd(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(loadAppIcon(16)));
    const HDC dc = ::GetDC(GetHwnd());
    m_dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSX) : USER_DEFAULT_SCREEN_DPI;
    if (dc) ::ReleaseDC(GetHwnd(), dc);
    if (m_dpi <= 0) m_dpi = USER_DEFAULT_SCREEN_DPI;
    createControls(); applyTheme(); resizeToContent();
    createTray(); restorePersistedTask();
    ::SetTimer(GetHwnd(), TIMER_SCHEDULER, 1000, nullptr);
    return 0;
}

int MainWindow::scale(int value) const { return ::MulDiv(value, m_dpi, 96); }

void MainWindow::resizeToContent() {
    RECT rect{0, 0, scale(526), scale(314)};
    ::AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    ::SetWindowPos(GetHwnd(), nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}

bool MainWindow::systemPrefersDark() {
    DWORD value = 1, size = sizeof(value);
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    const bool light = ::RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                                          reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS && value != 0;
    ::RegCloseKey(key);
    return !light;
}

void MainWindow::applyTheme() {
    m_dark = systemPrefersDark();
    if (m_bgBrush) ::DeleteObject(m_bgBrush);
    if (m_editBrush) ::DeleteObject(m_editBrush);
    m_bgBrush = ::CreateSolidBrush(m_dark ? kDarkBg : ::GetSysColor(COLOR_BTNFACE));
    m_editBrush = ::CreateSolidBrush(kDarkEditBg);
    const wchar_t *theme = m_dark ? L"DarkMode_Explorer" : L"Explorer";
    if (m_tab) ::SetWindowTheme(m_tab, theme, nullptr);
    for (HWND child : m_mainControls) if (child) ::SetWindowTheme(child, theme, nullptr);
    for (HWND child : m_settingsControls) if (child) ::SetWindowTheme(child, theme, nullptr);
    // Win10 1903 以下该属性不受支持，调用失败即可，保持浅色标题栏。
    const BOOL dark = m_dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(GetHwnd(), kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
    repaintWindow(GetHwnd());
}

void MainWindow::recreateControlsForDpi(int dpi) {
    // 跨显示器拖动时 DPI 变化，绝对布局只能销毁重建；先保留用户已输入的内容。
    const bool force = isChecked(m_force), fallback = isChecked(m_fallback);
    const std::wstring hours = text(m_hours), minutes = text(m_minutes), seconds = text(m_seconds);
    const auto pickedTarget = pickerDateTime(m_dateEdit, m_timeEdit);
    const bool settingsPage = m_tab && TabCtrl_GetCurSel(m_tab) == 1;
    HWND focus = ::GetFocus();
    HWND focusRoot = focus;
    while (focusRoot && ::GetParent(focusRoot) != GetHwnd()) focusRoot = ::GetParent(focusRoot);
    const int focusId = focusRoot ? ::GetDlgCtrlID(focusRoot) : 0;
    DWORD hoursStart = 0, hoursEnd = 0, minutesStart = 0, minutesEnd = 0, secondsStart = 0, secondsEnd = 0;
    if (m_hours) SendMessageW(m_hours, EM_GETSEL, reinterpret_cast<WPARAM>(&hoursStart), reinterpret_cast<LPARAM>(&hoursEnd));
    if (m_minutes) SendMessageW(m_minutes, EM_GETSEL, reinterpret_cast<WPARAM>(&minutesStart), reinterpret_cast<LPARAM>(&minutesEnd));
    if (m_seconds) SendMessageW(m_seconds, EM_GETSEL, reinterpret_cast<WPARAM>(&secondsStart), reinterpret_cast<LPARAM>(&secondsEnd));
    for (HWND child : m_mainControls) if (child) ::DestroyWindow(child);
    for (HWND child : m_settingsControls) if (child) ::DestroyWindow(child);
    if (m_tab) ::DestroyWindow(m_tab);
    m_mainControls.clear(); m_settingsControls.clear(); m_tab = nullptr;
    m_dpi = dpi > 0 ? dpi : m_dpi;
    createControls(); applyTheme();
    if (settingsPage) setSettingsVisible(true);
    SendMessageW(m_force, BM_SETCHECK, force ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_fallback, BM_SETCHECK, fallback ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SetWindowTextW(m_hours, hours.c_str());
    ::SetWindowTextW(m_minutes, minutes.c_str());
    ::SetWindowTextW(m_seconds, seconds.c_str());
    SendMessageW(m_hours, EM_SETSEL, hoursStart, hoursEnd);
    SendMessageW(m_minutes, EM_SETSEL, minutesStart, minutesEnd);
    SendMessageW(m_seconds, EM_SETSEL, secondsStart, secondsEnd);
    if (pickedTarget > 0) setPickerDateTime(m_dateEdit, m_timeEdit, pickedTarget);
    updateState(m_scheduler.state());
    updateRemaining(m_scheduler.remainingSeconds());
    refreshUpdateButton();
    if (focusId != 0) {
        HWND restoredFocus = ::GetDlgItem(GetHwnd(), focusId);
        if (restoredFocus && ::IsWindowVisible(restoredFocus) && ::IsWindowEnabled(restoredFocus)) ::SetFocus(restoredFocus);
    }
}

void MainWindow::createControls() {
    if (m_font) { ::DeleteObject(m_font); m_font = nullptr; }
    if (m_fontLarge) { ::DeleteObject(m_fontLarge); m_fontLarge = nullptr; }
    if (m_linkFont) { ::DeleteObject(m_linkFont); m_linkFont = nullptr; }
    const int fontHeight = scale(14), largeFontHeight = scale(20);
    m_font = CreateFontW(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    m_fontLarge = CreateFontW(-largeFontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    LOGFONTW linkLogFont{};
    if (m_font && ::GetObjectW(m_font, sizeof(linkLogFont), &linkLogFont) == sizeof(linkLogFont)) {
        linkLogFont.lfUnderline = TRUE;
        m_linkFont = ::CreateFontIndirectW(&linkLogFont);
    }

    m_tab = control(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, scale(8), scale(8), scale(510), scale(298), GetHwnd(), IDC_TAB);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(L"主界面");
    TabCtrl_InsertItem(m_tab, 0, &item);
    item.pszText = const_cast<LPWSTR>(L"设置");
    TabCtrl_InsertItem(m_tab, 1, &item);

    auto *groupAt = control(0, L"BUTTON", L"指定日期和时间", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, scale(20), scale(40), scale(494), scale(70), GetHwnd(), 0);
    auto *labelAt = control(0, L"STATIC", L"关机时间:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(34), scale(65), scale(74), scale(26), GetHwnd(), 0);
    m_dateEdit = control(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_SHORTDATECENTURYFORMAT, scale(108), scale(63), scale(142), scale(28), GetHwnd(), IDC_DATE);
    m_timeEdit = control(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT | DTS_UPDOWN, scale(260), scale(63), scale(92), scale(28), GetHwnd(), IDC_TIME);
    DateTime_SetFormat(m_timeEdit, L"HH:mm:ss");
    setPickerDateTime(m_dateEdit, m_timeEdit, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + 3600);
    auto *atButton = control(0, L"BUTTON", L"设置定时关机", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, scale(386), scale(62), scale(116), scale(30), GetHwnd(), IDC_AT);
    auto *groupCount = control(0, L"BUTTON", L"倒计时关机", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, scale(20), scale(120), scale(494), scale(70), GetHwnd(), 0);
    auto *countLabel = control(0, L"STATIC", L"时长:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(34), scale(145), scale(52), scale(26), GetHwnd(), 0);
    m_hours = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, scale(88), scale(143), scale(50), scale(28), GetHwnd(), IDC_HOURS);
    m_minutes = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, scale(160), scale(143), scale(50), scale(28), GetHwnd(), IDC_MINUTES);
    m_seconds = control(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, scale(232), scale(143), scale(50), scale(28), GetHwnd(), IDC_SECONDS);
    auto *hoursLabel = control(0, L"STATIC", L"时", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(142), scale(145), scale(18), scale(26), GetHwnd(), 0);
    auto *minutesLabel = control(0, L"STATIC", L"分", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(214), scale(145), scale(18), scale(26), GetHwnd(), 0);
    auto *secondsLabel = control(0, L"STATIC", L"秒", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(286), scale(145), scale(18), scale(26), GetHwnd(), 0);
    auto *countButton = control(0, L"BUTTON", L"开始倒计时", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, scale(386), scale(142), scale(116), scale(30), GetHwnd(), IDC_COUNTDOWN);
    auto *groupStatus = control(0, L"BUTTON", L"当前任务", BS_GROUPBOX | WS_CHILD | WS_VISIBLE, scale(20), scale(200), scale(494), scale(54), GetHwnd(), 0);
    auto *statusLabel = control(0, L"STATIC", L"状态:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(34), scale(220), scale(52), scale(26), GetHwnd(), 0);
    m_status = control(0, L"STATIC", L"空闲", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(88), scale(220), scale(145), scale(26), GetHwnd(), IDC_STATUS);
    auto *remainingLabel = control(0, L"STATIC", L"剩余:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(270), scale(220), scale(52), scale(26), GetHwnd(), 0);
    m_remaining = control(0, L"STATIC", L"--", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, scale(324), scale(219), scale(170), scale(28), GetHwnd(), IDC_REMAINING);
    m_pause = control(0, L"BUTTON", L"暂停", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED, scale(48), scale(266), scale(100), scale(30), GetHwnd(), IDC_PAUSE);
    auto *cancelButton = control(0, L"BUTTON", L"取消任务", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, scale(164), scale(266), scale(100), scale(30), GetHwnd(), IDC_CANCEL);
    auto *nowButton = control(0, L"BUTTON", L"立即关机", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, scale(280), scale(266), scale(100), scale(30), GetHwnd(), IDC_NOW);

    m_settingsGroup = control(0, L"BUTTON", L"设置选项", BS_GROUPBOX | WS_CHILD, scale(20), scale(40), scale(494), scale(214), GetHwnd(), 0);
    m_force = control(0, L"BUTTON", L"强制关闭应用（可能丢失未保存数据）", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, scale(44), scale(72), scale(430), scale(24), GetHwnd(), IDC_FORCE);
    m_fallback = control(0, L"BUTTON", L"启用 Task Scheduler 系统兜底", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, scale(44), scale(104), scale(400), scale(24), GetHwnd(), IDC_FALLBACK);
    m_checkUpdate = control(0, L"BUTTON", L"检查更新", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, scale(44), scale(148), scale(118), scale(30), GetHwnd(), IDC_CHECK);
    auto *projectLabel = control(0, L"STATIC", L"项目主页:", WS_CHILD | SS_CENTERIMAGE, scale(44), scale(194), scale(74), scale(24), GetHwnd(), 0);
    m_githubLink = control(0, L"STATIC", L"GitHub 项目主页", WS_CHILD | WS_TABSTOP | SS_NOTIFY | SS_CENTERIMAGE,
                           scale(118), scale(194), scale(240), scale(24), GetHwnd(), IDC_GITHUB);
    m_mainControls = {groupAt, labelAt, m_dateEdit, m_timeEdit, atButton, groupCount, countLabel, m_hours, m_minutes, m_seconds,
                      hoursLabel, minutesLabel, secondsLabel, countButton, groupStatus, statusLabel, m_status, remainingLabel,
                      m_remaining, m_pause, cancelButton, nowButton};
    m_settingsControls = {m_settingsGroup, m_force, m_fallback, m_checkUpdate, projectLabel, m_githubLink};
    for (HWND child : m_mainControls) setFont(child, m_font);
    for (HWND child : m_settingsControls) setFont(child, m_font);
    for (HWND child : m_mainControls) {
        wchar_t className[16]{};
        if (child && ::GetClassNameW(child, className, 16) && ::lstrcmpiW(className, L"Static") == 0)
            makeStaticTransparent(child);
    }
    makeStaticTransparent(projectLabel);
    makeStaticTransparent(m_githubLink);
    setFont(m_githubLink, m_linkFont ? m_linkFont : m_font);
    setFont(m_remaining, m_fontLarge);
    setFont(m_tab, m_font);
    refreshUpdateButton();
    setSettingsVisible(false);
}

void MainWindow::setSettingsVisible(bool visible) {
    if (m_tab) TabCtrl_SetCurSel(m_tab, visible ? 1 : 0);
    for (HWND hwnd : m_mainControls) if (hwnd) ::ShowWindow(hwnd, visible ? SW_HIDE : SW_SHOW);
    for (HWND hwnd : m_settingsControls) if (hwnd) ::ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    // 切换页面时部分 Win32 子控件会留下旧文字/边框残影，强制擦除并同步重绘父窗口和所有子控件。
    repaintWindow(GetHwnd());
}

void MainWindow::createTray() {
    m_tray = NOTIFYICONDATAW{}; m_tray.cbSize = sizeof(NOTIFYICONDATAW); m_tray.hWnd = GetHwnd(); m_tray.uID = 1; m_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; m_tray.uCallbackMessage = WM_TRAY; m_tray.hIcon = loadAppIcon(16); wcscpy_s(m_tray.szTip, L"定时关机");
    m_trayCreated = Shell_NotifyIconW(NIM_ADD, &m_tray) == TRUE;
    updateTrayTip();
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

void MainWindow::checkForUpdates() {
    if (m_updateCheckInProgress) return;
    m_updateCheckInProgress = true;
    refreshUpdateButton();
    m_updateManager.checkForUpdates();
}

void MainWindow::refreshUpdateButton() {
    if (!m_checkUpdate) return;
    ::EnableWindow(m_checkUpdate, m_updateCheckInProgress ? FALSE : TRUE);
    setText(m_checkUpdate, m_updateCheckInProgress ? L"检查中..." : L"检查更新");
}

std::wstring MainWindow::formatDuration(std::int64_t seconds) {
    wchar_t buffer[64]{}; swprintf_s(buffer, 64, L"%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60); return buffer;
}

void MainWindow::updateRemaining(std::int64_t seconds) { setText(m_remaining, seconds > 0 ? formatDuration(seconds) : L"--"); updateTrayTip(); }

void MainWindow::updateState(ShutdownScheduler::State state) {
    const wchar_t *label = L"空闲"; if (state == ShutdownScheduler::State::Armed) label = L"已设置"; else if (state == ShutdownScheduler::State::Paused) label = L"已暂停"; else if (state == ShutdownScheduler::State::Executing) label = L"正在关机"; else if (state == ShutdownScheduler::State::Completed) label = L"已完成"; else if (state == ShutdownScheduler::State::Error) label = L"失败";
    setText(m_status, label); ::EnableWindow(m_pause, m_scheduler.isActive()); setText(m_pause, state == ShutdownScheduler::State::Paused ? L"继续" : L"暂停");
    updateTrayTip();
}

std::wstring MainWindow::currentCountdownText() const {
    if (!m_scheduler.isActive()) return L"无活动任务";
    const auto remaining = m_scheduler.remainingSeconds();
    const std::wstring prefix = m_scheduler.state() == ShutdownScheduler::State::Paused ? L"已暂停: " : L"倒计时: ";
    return prefix + formatDuration(remaining);
}

void MainWindow::updateTrayTip() {
    if (!m_trayCreated) return;
    const std::wstring tip = m_scheduler.isActive() ? (L"定时关机 - " + currentCountdownText()) : L"定时关机";
    wcscpy_s(m_tray.szTip, tip.c_str());
    m_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_tray);
}

void MainWindow::showFromTray() { ::ShowWindow(GetHwnd(), SW_SHOWNORMAL); ::SetForegroundWindow(GetHwnd()); }

void MainWindow::post(UiEvent *event) { if (!PostMessageW(GetHwnd(), WM_UI_EVENT, 0, reinterpret_cast<LPARAM>(event))) delete event; }

void MainWindow::handleEvent(std::unique_ptr<UiEvent> event) {
    m_updateCheckInProgress = false;
    refreshUpdateButton();
    switch (event->type) {
    case UiEvent::Type::UpdateAvailable:
        if (::MessageBoxW(GetHwnd(), updatePromptText(event->version).c_str(), L"发现新版本", MB_YESNO | MB_ICONINFORMATION) == IDYES) openUrl(GetHwnd(), kLatestReleaseUrl);
        break;
    case UiEvent::Type::NoUpdate: ::MessageBoxW(GetHwnd(), L"当前已经是最新版本。", L"检查更新", MB_OK | MB_ICONINFORMATION); break;
    case UiEvent::Type::CheckError: ::MessageBoxW(GetHwnd(), event->text.c_str(), L"检查更新失败", MB_OK | MB_ICONWARNING); break;
    }
}

BOOL MainWindow::OnCommand(WPARAM wparam, LPARAM) {
    if (LOWORD(wparam) == IDC_GITHUB && HIWORD(wparam) == STN_CLICKED) {
        openUrl(GetHwnd(), kProjectUrl);
        return TRUE;
    }
    switch (LOWORD(wparam)) { case IDC_AT: scheduleAt(); return TRUE; case IDC_COUNTDOWN: scheduleCountdown(); return TRUE; case IDC_PAUSE: togglePause(); return TRUE; case IDC_CANCEL: cancelTask(); return TRUE; case IDC_NOW: executeNow(); return TRUE; case IDC_CHECK: checkForUpdates(); return TRUE; }
    return FALSE;
}

LRESULT MainWindow::OnNotify(WPARAM wparam, LPARAM lparam) {
    auto *notify = reinterpret_cast<NMHDR *>(lparam);
    if (notify && (notify->hwndFrom == m_tab || static_cast<int>(notify->idFrom) == IDC_TAB)) {
        setSettingsVisible(TabCtrl_GetCurSel(m_tab) == 1);
        return TRUE;
    }
    return CWnd::OnNotify(wparam, lparam);
}

bool MainWindow::askCloseWithActiveTask() {
    const int choice = ::MessageBoxW(GetHwnd(), L"当前存在活动任务。退出时保留任务吗？", L"退出程序", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return false;
    if (choice == IDNO) m_scheduler.cancel();
    return true;
}

void MainWindow::OnClose() { if (!m_forceQuit && m_scheduler.isActive() && !askCloseWithActiveTask()) return; DestroyWindow(GetHwnd()); }
void MainWindow::OnDestroy() {
    m_updateCheckInProgress = false;
    ::KillTimer(GetHwnd(), TIMER_SCHEDULER);
    m_updateManager.stopAndJoin();
    MSG message{};
    while (::PeekMessageW(&message, GetHwnd(), WM_UI_EVENT, WM_UI_EVENT, PM_REMOVE)) {
        delete reinterpret_cast<UiEvent *>(message.lParam);
    }
    destroyTray();
    PostQuitMessage(0);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_TIMER) { if (wparam == TIMER_SCHEDULER) m_scheduler.tick(); return 0; }
    if (msg == WM_SIZE && wparam == SIZE_MINIMIZED) { ::ShowWindow(GetHwnd(), SW_HIDE); return 0; }
    if (msg == WM_NOTIFY) {
        auto *notify = reinterpret_cast<NMHDR *>(lparam);
        if (notify && (notify->hwndFrom == m_tab || static_cast<int>(notify->idFrom) == IDC_TAB)) {
            setSettingsVisible(TabCtrl_GetCurSel(m_tab) == 1);
            return 0;
        }
    }
    if (msg == WM_TRAY && m_trayCreated) {
        if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) showFromTray();
        if (lparam == WM_RBUTTONUP) {
            POINT point{}; GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            const std::wstring countdown = currentCountdownText();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, countdown.c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示窗口");
            AppendMenuW(menu, MF_STRING, ID_TRAY_CANCEL, L"取消任务");
            AppendMenuW(menu, MF_STRING | (m_updateCheckInProgress ? MF_GRAYED : MF_ENABLED), ID_TRAY_CHECK, L"检查更新");
            AppendMenuW(menu, MF_STRING, ID_TRAY_NOW, L"立即关机");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
            ::SetForegroundWindow(GetHwnd());
            const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, GetHwnd(), nullptr);
            DestroyMenu(menu);
            if (cmd == ID_TRAY_SHOW) showFromTray();
            else if (cmd == ID_TRAY_CANCEL) cancelTask();
            else if (cmd == ID_TRAY_CHECK) checkForUpdates();
            else if (cmd == ID_TRAY_NOW) executeNow();
            else if (cmd == ID_TRAY_EXIT) { m_forceQuit = true; DestroyWindow(GetHwnd()); }
        }
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        const auto *suggested = reinterpret_cast<RECT *>(lparam);
        ::SetWindowPos(GetHwnd(), nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left, suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        recreateControlsForDpi(LOWORD(wparam));
        return 0;
    }
    if (msg == WM_SETTINGCHANGE && lparam && wcscmp(reinterpret_cast<LPCWSTR>(lparam), L"ImmersiveColorSet") == 0) {
        applyTheme();
        return 0;
    }
    if (msg == WM_ERASEBKGND && m_dark && m_bgBrush) {
        RECT rect{};
        ::GetClientRect(GetHwnd(), &rect);
        ::FillRect(reinterpret_cast<HDC>(wparam), &rect, m_bgBrush);
        return 1;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        const HWND target = reinterpret_cast<HWND>(lparam);
        ::SetBkMode(dc, TRANSPARENT);
        if (target == m_githubLink) {
            ::SetTextColor(dc, m_dark ? kDarkAccent : kLightLink);
        } else if (m_dark) {
            ::SetTextColor(dc, target == m_remaining ? kDarkAccent : kDarkText);
        } else {
            ::SetTextColor(dc, ::GetSysColor(COLOR_WINDOWTEXT));
        }
        return reinterpret_cast<LRESULT>(::GetStockObject(NULL_BRUSH));
    }
    if (m_dark && m_editBrush && msg == WM_CTLCOLOREDIT) {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        ::SetBkColor(dc, kDarkEditBg);
        ::SetTextColor(dc, kDarkText);
        return reinterpret_cast<LRESULT>(m_editBrush);
    }
    if (msg == WM_SETCURSOR && reinterpret_cast<HWND>(wparam) == m_githubLink) {
        ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    if (msg == WM_UI_EVENT) { handleEvent(std::unique_ptr<UiEvent>(reinterpret_cast<UiEvent *>(lparam))); return 0; }
    return CWnd::WndProc(msg, wparam, lparam);
}
