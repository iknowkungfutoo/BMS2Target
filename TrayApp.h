#pragma once

#include <functional>
#include <string>

// Everything Win32-GUI-specific lives here: the tray icon, its context menu,
// and the message loop that keeps it responsive. Kept separate from the BMS
// shared-memory/TCP logic in BMS2Target.cpp.

// Creates the hidden message-only window and the tray icon. `app_title` is
// used for the initial tooltip and the About box (e.g. "BMS2Target v2.0.0").
// Returns false if window/icon creation failed.
bool CreateTrayApp(const std::wstring& app_title);

// Called when the user picks "Exit" from the tray menu. The callback is
// responsible for whatever shutdown signal the rest of the app uses (e.g.
// setting the polling loop's quit flag) - TrayApp itself has no opinion on
// that mechanism.
void SetTrayExitCallback(std::function<void()> callback);

// Called when the user toggles the "Enable Log File" checkbox. Receives the
// new desired state.
void SetTrayLoggingToggleCallback(std::function<void(bool)> callback);

// Runs the Win32 message loop until WM_QUIT (posted after Exit is handled).
// Must be called from the same thread that called CreateTrayApp.
int RunTrayMessageLoop();

// Updates the tray icon's hover tooltip text (shows current connection/flight state).
void UpdateTrayTooltip(const std::wstring& text);

// Shows a Windows notification-area balloon for a high-level lifecycle event
// (connect/disconnect, flight start/end) - not meant for per-field spam.
void ShowTrayBalloon(const std::wstring& title, const std::wstring& text);

// Removes the tray icon and destroys the hidden window. Safe to call once
// after RunTrayMessageLoop() returns.
void DestroyTrayApp();
