#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "TrayApp.h"
#include "resource.h"
#include "DarkMode.h"
#include "Logging.h"

#pragma comment(lib, "Shell32.lib")

namespace
{
    const UINT WM_TRAYICON = WM_APP + 1;

    const UINT ID_TRAY_TOGGLE_LOG     = 1001;
    const UINT ID_TRAY_ABOUT          = 1002;
    const UINT ID_TRAY_EXIT           = 1003;
    const UINT ID_TRAY_TOGGLE_STARTUP = 1004;

    HWND g_hwnd = NULL;
    NOTIFYICONDATAW g_nid = {};
    std::wstring g_app_title;

    std::function<void()> g_exit_callback;
    std::function<void(bool)> g_log_toggle_callback;

    // Per-user, no admin rights needed - HKCU's Run key is the standard
    // mechanism for an optional, user-toggleable "start with Windows"
    // (distinct from the installer, which deliberately doesn't set this up
    // itself). Queried fresh each time the menu opens rather than cached,
    // since it's persistent system state that could in principle be changed
    // elsewhere (e.g. Windows' own Startup Apps settings page).
    const wchar_t* kRunKeyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kRunValueName = L"BMS2Target";

    std::wstring GetExePath()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        return path;
    }

    bool IsStartWithWindowsEnabled()
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;

        LSTATUS status = RegQueryValueExW(key, kRunValueName, NULL, NULL, NULL, NULL);
        RegCloseKey(key);

        return status == ERROR_SUCCESS;
    }

    void SetStartWithWindows(bool enabled)
    {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;

        if (enabled)
        {
            std::wstring quoted_path = L"\"" + GetExePath() + L"\"";
            RegSetValueExW(key, kRunValueName, 0, REG_SZ, (const BYTE*)quoted_path.c_str(),
                (DWORD)((quoted_path.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            RegDeleteValueW(key, kRunValueName);
        }

        RegCloseKey(key);
    }

    void ShowTrayMenu(HWND hwnd)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (IsFileLoggingEnabled() ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOGGLE_LOG, L"Enable Log File");
        AppendMenuW(menu, MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOGGLE_STARTUP, L"Start with Windows");
        AppendMenuW(menu, MF_STRING, ID_TRAY_ABOUT, L"About");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

        POINT cursor;
        GetCursorPos(&cursor);

        // Required so the menu dismisses correctly when the user clicks away.
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, NULL);
        PostMessage(hwnd, WM_NULL, 0, 0);

        DestroyMenu(menu);
    }

    LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
            {
                ShowTrayMenu(hwnd);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case ID_TRAY_TOGGLE_LOG:
                if (g_log_toggle_callback) g_log_toggle_callback(!IsFileLoggingEnabled());
                return 0;

            case ID_TRAY_TOGGLE_STARTUP:
                SetStartWithWindows(!IsStartWithWindowsEnabled());
                return 0;

            case ID_TRAY_ABOUT:
                ShowThemedMessageBox(NULL, g_app_title, L"About BMS2Target");
                return 0;

            case ID_TRAY_EXIT:
                if (g_exit_callback) g_exit_callback();
                PostQuitMessage(0);
                return 0;
            }
            return 0;

        case WM_SETTINGCHANGE:
            RefreshDarkModeOnSettingChange();
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool CreateTrayApp(const std::wstring& app_title)
{
    g_app_title = app_title;

    HINSTANCE hInstance = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"BMS2TargetTrayWindowClass";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // A normal (never-shown) top-level window, not a message-only (HWND_MESSAGE)
    // one - TrackPopupMenu/SetForegroundWindow need a real top-level window to
    // reliably dismiss the context menu when the user clicks elsewhere.
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, app_title.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL, hInstance, NULL);
    if (g_hwnd == NULL) return false;

    EnableDarkModeForWindow(g_hwnd);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wcsncpy_s(g_nid.szTip, app_title.c_str(), _TRUNCATE);

    return Shell_NotifyIconW(NIM_ADD, &g_nid) == TRUE;
}

void SetTrayExitCallback(std::function<void()> callback)
{
    g_exit_callback = callback;
}

void SetTrayLoggingToggleCallback(std::function<void(bool)> callback)
{
    g_log_toggle_callback = callback;
}

int RunTrayMessageLoop()
{
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void UpdateTrayTooltip(const std::wstring& text)
{
    wcsncpy_s(g_nid.szTip, text.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void ShowTrayBalloon(const std::wstring& title, const std::wstring& text)
{
    NOTIFYICONDATAW balloon = g_nid;
    balloon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    wcsncpy_s(balloon.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(balloon.szInfo, text.c_str(), _TRUNCATE);
    balloon.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

void DestroyTrayApp()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hwnd != NULL)
    {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
}
