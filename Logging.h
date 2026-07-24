#pragma once

#include <string>

// Opt-in log file, next to the executable. Off by default; SetLoggingEnabled(true)
// (re)creates a fresh bms2target.log for the current session, SetLoggingEnabled(false)
// closes it. Log() is a no-op whenever logging is disabled - callers don't need to
// check IsFileLoggingEnabled() themselves. Every real change is persisted to the
// registry (HKCU\Software\BMS2Target) so the choice survives a restart.
void SetLoggingEnabled(bool enabled);
bool IsFileLoggingEnabled();
void Log(const std::string& line);

// Applies whatever logging preference was last persisted (off if never set) -
// call once at startup, before the tray menu can possibly be opened, so its
// checkbox and actual logging state start in sync.
void ApplyPersistedLoggingPreference();
