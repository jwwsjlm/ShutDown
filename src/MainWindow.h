#pragma once

#include "ShutdownScheduler.h"
#include "UpdateManager.h"

#include "wxx_wincore.h"

#include <memory>
#include <string>
#include <vector>

class MainWindow final : public Win32xx::CWnd {
public:
    explicit MainWindow(std::string version);
    ~MainWindow() override;
    HWND CreateMain();

protected:
    void PreRegisterClass(WNDCLASS &wc) override;
    void PreCreate(CREATESTRUCT &cs) override;
    int OnCreate(CREATESTRUCT &cs) override;
    BOOL OnCommand(WPARAM wparam, LPARAM lparam) override;
    LRESULT OnNotify(WPARAM wparam, LPARAM lparam) override;
    void OnClose() override;
    void OnDestroy() override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    struct UiEvent {
        enum class Type { UpdateAvailable, NoUpdate, CheckError };
        Type type;
        std::string version;
        std::wstring text;
    };

    void createControls();
    void recreateControlsForDpi(int dpi);
    void resizeToContent();
    void applyTheme();
    int scale(int value) const;
    static bool systemPrefersDark();
    void createTray();
    void destroyTray();
    void restorePersistedTask();
    void setText(HWND control, const std::wstring &text);
    std::wstring text(HWND control) const;
    void scheduleAt();
    void scheduleCountdown();
    void cancelTask();
    void togglePause();
    void executeNow();
    void checkForUpdates();
    void refreshUpdateButton();
    void handleEvent(std::unique_ptr<UiEvent> event);
    void updateState(ShutdownScheduler::State state);
    void updateRemaining(std::int64_t seconds);
    bool askCloseWithActiveTask();
    void showFromTray();
    void setSettingsVisible(bool visible);
    std::wstring currentCountdownText() const;
    void updateTrayTip();
    static std::wstring formatDuration(std::int64_t seconds);
    void post(UiEvent *event);

    UpdateManager m_updateManager;
    ShutdownScheduler m_scheduler;
    std::wstring m_windowTitle;
    HFONT m_font = nullptr;
    HFONT m_fontLarge = nullptr;
    HFONT m_linkFont = nullptr;
    HBRUSH m_bgBrush = nullptr;
    HBRUSH m_editBrush = nullptr;
    int m_dpi = 96;
    bool m_dark = false;
    NOTIFYICONDATAW m_tray{};
    bool m_trayCreated = false;
    bool m_forceQuit = false;
    bool m_updateCheckInProgress = false;
    HWND m_dateEdit = nullptr;
    HWND m_timeEdit = nullptr;
    HWND m_hours = nullptr;
    HWND m_minutes = nullptr;
    HWND m_seconds = nullptr;
    HWND m_force = nullptr;
    HWND m_fallback = nullptr;
    HWND m_status = nullptr;
    HWND m_remaining = nullptr;
    HWND m_pause = nullptr;
    HWND m_checkUpdate = nullptr;
    HWND m_githubLink = nullptr;
    HWND m_tab = nullptr;
    HWND m_settingsGroup = nullptr;
    std::vector<HWND> m_mainControls;
    std::vector<HWND> m_settingsControls;
};
