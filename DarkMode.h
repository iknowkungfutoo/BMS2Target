#pragma once

#include <windows.h>
#include <string>

// Native Win32 menus/message boxes don't follow Windows' light/dark theme
// unless the app opts in via a handful of *undocumented* uxtheme.dll
// functions (exported only by ordinal) - see DarkMode.cpp for the reference
// this is based on. Every lookup here fails safe: on an unexpected Windows
// version, the whole module quietly no-ops and the app just stays light.

// Call once, as early as possible in main() - before any window exists and
// before anything might show a message box. Process-wide; not tied to any
// specific HWND.
void InitDarkModeSupport();

// Call once a window exists that will own dark-mode-aware UI (here: the
// hidden tray window that TrackPopupMenu's context menu is shown against).
void EnableDarkModeForWindow(HWND hwnd);

// Call when WM_SETTINGCHANGE fires, so a live theme switch (no app restart)
// is picked up - menu theme data is cached and needs an explicit flush.
void RefreshDarkModeOnSettingChange();

// True if Windows' "apps" theme (Settings > Personalization > Colors) is
// currently dark - not the taskbar/start "system" theme, which is separate.
bool IsSystemDarkModeEnabled();

// A small OK-button/info-icon message dialog that always matches the current
// system theme (light or dark). Not a MessageBoxW wrapper - MessageBoxW's own
// stock dialog class draws parts of itself (notably a footer band behind the
// button) that aren't reachable through any public hook, so this is a fully
// self-owned window instead, painted the same way the rest of this app's UI
// is. Only supports what this app actually needs: an OK button and an info
// icon - not a general MessageBoxW replacement.
void ShowThemedMessageBox(HWND owner, const std::wstring& text, const std::wstring& title);
