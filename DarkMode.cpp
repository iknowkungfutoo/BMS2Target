#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <winternl.h>

#include "DarkMode.h"

#pragma comment(lib, "dwmapi.lib")

// Public in newer Windows SDKs, but declared defensively in case whatever
// SDK this builds against predates it - the value itself (not just the
// symbol) is stable and documented since Windows 10 20H1/build 19041.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Undocumented uxtheme.dll ordinals, per the community-maintained reference
// at https://github.com/ysc3839/win32-darkmode (the same technique used by
// Notepad++, Windows Terminal, etc.) - stable since Windows 10 1903 (build
// 18362), which is what's targeted here. Older 1809-only builds used a
// different ordinal 135 (AllowDarkModeForApp instead of SetPreferredAppMode)
// - deliberately not supported below; falls back to plain light UI there
// rather than adding that branch for a six-year-obsolete Windows build.
// This part is still what makes the tray context menu itself follow the
// theme - TrackPopupMenu is a real native menu and does pick up dark
// rendering from these calls correctly.
namespace
{
    enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, MaxMode };

    typedef BOOL(WINAPI* AllowDarkModeForWindowFn)(HWND hWnd, BOOL allow);
    typedef PreferredAppMode(WINAPI* SetPreferredAppModeFn)(PreferredAppMode appMode);
    typedef void(WINAPI* FlushMenuThemesFn)();
    typedef NTSTATUS(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);

    AllowDarkModeForWindowFn g_AllowDarkModeForWindow = nullptr;
    SetPreferredAppModeFn    g_SetPreferredAppMode    = nullptr;
    FlushMenuThemesFn        g_FlushMenuThemes        = nullptr;

    bool g_dark_mode_supported = false;

    bool IsWindows1903OrLater()
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return false;

        RtlGetVersionFn RtlGetVersion = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (!RtlGetVersion) return false;

        RTL_OSVERSIONINFOW info = { sizeof(info) };
        if (RtlGetVersion(&info) != 0) return false;

        return info.dwBuildNumber >= 18362;
    }
}

void InitDarkModeSupport()
{
    if (!IsWindows1903OrLater()) return;

    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;

    g_AllowDarkModeForWindow = (AllowDarkModeForWindowFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(133));
    g_SetPreferredAppMode    = (SetPreferredAppModeFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    g_FlushMenuThemes        = (FlushMenuThemesFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));

    if (!g_AllowDarkModeForWindow || !g_SetPreferredAppMode || !g_FlushMenuThemes) return;

    g_dark_mode_supported = true;

    // AllowDark (not ForceDark) - "match the system," which is what
    // following the theme means; ForceDark would ignore a light system theme.
    g_SetPreferredAppMode(AllowDark);
    g_FlushMenuThemes();
}

void EnableDarkModeForWindow(HWND hwnd)
{
    if (g_dark_mode_supported) g_AllowDarkModeForWindow(hwnd, TRUE);
}

void RefreshDarkModeOnSettingChange()
{
    if (g_dark_mode_supported) g_FlushMenuThemes();
}

bool IsSystemDarkModeEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD value = 1; // default to light if the value is missing
    DWORD size = sizeof(value);
    RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
    RegCloseKey(key);

    return value == 0;
}

// ShowThemedMessageBox: rather than fight MessageBoxW's own internal paint
// logic (its stock dialog class draws a footer band behind the button that
// isn't reachable through WM_ERASEBKGND/WM_CTLCOLOR*, on top of buttons
// needing owner-draw to fill dark at all), this is a small self-contained
// window we fully own and paint - same Win32 APIs the rest of this app
// already uses, just not routed through the stock MessageBox implementation.
// It always adapts to the current theme (light or dark), not only dark mode.
namespace
{
    const int ID_OK_BUTTON = 1;
    const wchar_t* kDialogClassName = L"BMS2TargetThemedDialogClass";

    struct ThemedDialogState
    {
        std::wstring message;
        HFONT font = nullptr;
        HICON icon = nullptr;
        RECT icon_rect = {};
        RECT text_rect = {};
        bool done = false;
    };

    HBRUSH DarkBrush()
    {
        static HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32));
        return brush;
    }

    HBRUSH LightBrush()
    {
        static HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
        return brush;
    }

    void DrawThemedButton(const DRAWITEMSTRUCT* dis, bool dark)
    {
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;

        COLORREF fill_color   = dark ? (pressed ? RGB(45, 45, 45)    : RGB(60, 60, 60))
                                      : (pressed ? RGB(225, 225, 225) : RGB(240, 240, 240));
        COLORREF border_color = dark ? RGB(90, 90, 90) : RGB(160, 160, 160);
        COLORREF text_color   = dark ? RGB(240, 240, 240) : RGB(0, 0, 0);

        HBRUSH fill = CreateSolidBrush(fill_color);
        FillRect(dis->hDC, &dis->rcItem, fill);
        DeleteObject(fill);

        HPEN pen = CreatePen(PS_SOLID, 1, border_color);
        HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(pen);

        // Owner-draw controls don't get their font auto-selected into the DC
        // before WM_DRAWITEM, unlike normal button painting.
        HFONT font = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        HGDIOBJ oldFont = font ? SelectObject(dis->hDC, font) : NULL;

        wchar_t text[64] = {};
        GetWindowTextW(dis->hwndItem, text, 64);
        SetTextColor(dis->hDC, text_color);
        SetBkMode(dis->hDC, TRANSPARENT);
        RECT text_rect = dis->rcItem;
        DrawTextW(dis->hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (oldFont) SelectObject(dis->hDC, oldFont);

        if (focused)
        {
            RECT focus_rect = dis->rcItem;
            InflateRect(&focus_rect, -3, -3);
            DrawFocusRect(dis->hDC, &focus_rect);
        }
    }

    LRESULT CALLBACK ThemedMsgBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ThemedDialogState* state = (ThemedDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

        switch (msg)
        {
        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, IsSystemDarkModeEnabled() ? DarkBrush() : LightBrush());
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (state)
            {
                bool dark = IsSystemDarkModeEnabled();

                DrawIconEx(hdc, state->icon_rect.left, state->icon_rect.top, state->icon, 32, 32, 0, NULL, DI_NORMAL);

                HGDIOBJ oldFont = SelectObject(hdc, state->font);
                SetTextColor(hdc, dark ? RGB(240, 240, 240) : RGB(0, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                RECT text_rect = state->text_rect;
                DrawTextW(hdc, state->message.c_str(), -1, &text_rect, DT_LEFT | DT_WORDBREAK);
                SelectObject(hdc, oldFont);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlID == ID_OK_BUTTON)
            {
                DrawThemedButton(dis, IsSystemDarkModeEnabled());
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_OK_BUTTON)
            {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (state)
            {
                state->done = true;
                if (state->font) DeleteObject(state->font);
                if (state->icon) DestroyIcon(state->icon);
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void EnsureDialogClassRegistered()
    {
        static bool registered = false;
        if (registered) return;
        registered = true;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ThemedMsgBoxWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = kDialogClassName;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassExW(&wc);
    }
}

void ShowThemedMessageBox(HWND owner, const std::wstring& text, const std::wstring& title)
{
    EnsureDialogClassRegistered();

    ThemedDialogState state;
    state.message = text;
    state.icon = LoadIconW(NULL, IDI_INFORMATION);

    // Segoe UI matches the modern Windows UI font; DEFAULT_GUI_FONT (MS Shell
    // Dlg) looks dated by comparison.
    state.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int margin = 20;
    const int icon_size = 32;
    const int icon_text_gap = 15;
    const int text_max_width = 280;
    const int button_width = 88;
    const int button_height = 26;
    const int button_bottom_margin = 16;

    HDC screen_dc = GetDC(NULL);
    HGDIOBJ old_font = SelectObject(screen_dc, state.font);
    RECT measured_text_rect = { 0, 0, text_max_width, 0 };
    DrawTextW(screen_dc, state.message.c_str(), -1, &measured_text_rect, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(screen_dc, old_font);
    ReleaseDC(NULL, screen_dc);

    int text_width = measured_text_rect.right - measured_text_rect.left;
    int text_height = measured_text_rect.bottom - measured_text_rect.top;
    int content_height = (icon_size > text_height) ? icon_size : text_height;

    int client_width = margin * 2 + icon_size + icon_text_gap + text_width;
    int client_height = margin + content_height + margin + button_height + button_bottom_margin;

    state.icon_rect = { margin, margin, margin + icon_size, margin + icon_size };
    state.text_rect = { margin + icon_size + icon_text_gap, margin,
                         margin + icon_size + icon_text_gap + text_width, margin + content_height };

    RECT window_rect = { 0, 0, client_width, client_height };
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRectEx(&window_rect, style, FALSE, WS_EX_DLGMODALFRAME);

    int window_width = window_rect.right - window_rect.left;
    int window_height = window_rect.bottom - window_rect.top;

    int x, y;
    RECT anchor_rect;
    if (owner && IsWindow(owner) && GetWindowRect(owner, &anchor_rect))
    {
        x = anchor_rect.left + ((anchor_rect.right - anchor_rect.left) - window_width) / 2;
        y = anchor_rect.top + ((anchor_rect.bottom - anchor_rect.top) - window_height) / 2;
    }
    else
    {
        x = (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClassName, title.c_str(), style,
        x, y, window_width, window_height, owner, NULL, GetModuleHandleW(NULL), NULL);

    if (!hwnd)
    {
        if (state.font) DeleteObject(state.font);
        if (state.icon) DestroyIcon(state.icon);
        return;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&state);

    BOOL dark = IsSystemDarkModeEnabled();
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    HWND ok_button = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
        client_width - margin - button_width, client_height - button_bottom_margin - button_height,
        button_width, button_height, hwnd, (HMENU)(INT_PTR)ID_OK_BUTTON, GetModuleHandleW(NULL), NULL);

    if (ok_button) SendMessageW(ok_button, WM_SETFONT, (WPARAM)state.font, TRUE);

    if (owner) EnableWindow(owner, FALSE);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (ok_button) SetFocus(ok_button);

    MSG msg;
    while (!state.done && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE))
        {
            DestroyWindow(hwnd);
            continue;
        }
        if (!IsDialogMessage(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}
