#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>

#include "Logging.h"

#pragma comment(lib, "Shell32.lib")

namespace
{
    std::mutex g_log_mutex;
    std::ofstream g_log_file;
    bool g_logging_enabled = false;

    // Deliberately avoids <filesystem> - this project targets an older C++
    // standard (see the <format>/<filesystem> STL4038 warnings if you try),
    // so this sticks to plain Win32 string manipulation instead.
    //
    // Downloads, not next to the exe: an MSI install lands in Program Files,
    // which a non-admin process can't write to at all (logging would just
    // silently fail there), and even for the portable exe, Program
    // Files/AppData-style locations are awkward for non-technical users to
    // navigate to when asked to send in a log file. Falls back to next to
    // the exe only if the known-folder lookup itself fails, which shouldn't
    // happen on any real Windows install.
    std::wstring LogFilePath()
    {
        PWSTR downloads_path = NULL;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &downloads_path);

        if (SUCCEEDED(hr) && downloads_path != NULL)
        {
            std::wstring path(downloads_path);
            CoTaskMemFree(downloads_path);
            path += L"\\bms2target.log";
            return path;
        }

        if (downloads_path != NULL) CoTaskMemFree(downloads_path);

        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path, MAX_PATH);

        std::wstring path(exe_path);
        size_t last_slash = path.find_last_of(L"\\/");
        path.erase(last_slash == std::wstring::npos ? 0 : last_slash + 1);
        path += L"bms2target.log";
        return path;
    }

    std::string Timestamp()
    {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm local_tm;
        localtime_s(&local_tm, &now);

        char buffer[16];
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_tm);
        return std::string(buffer);
    }

    // Per-user, no admin rights needed - the logging toggle used to reset to
    // off on every restart, which made it easy to miss re-enabling it before
    // reproducing an intermittent bug. Persisted the same way TrayApp.cpp
    // already persists "Start with Windows", just under this app's own
    // settings key rather than the Run key.
    const wchar_t* kSettingsKeyPath = L"Software\\BMS2Target";
    const wchar_t* kLoggingValueName = L"EnableLogging";

    void PersistLoggingPreference(bool enabled)
    {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;

        DWORD value = enabled ? 1 : 0;
        RegSetValueExW(key, kLoggingValueName, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));

        RegCloseKey(key);
    }

    bool LoadLoggingPreference()
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;

        DWORD value = 0;
        DWORD size = sizeof(value);
        LSTATUS status = RegQueryValueExW(key, kLoggingValueName, NULL, NULL, (BYTE*)&value, &size);
        RegCloseKey(key);

        return status == ERROR_SUCCESS && value != 0;
    }
}

void SetLoggingEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (enabled == g_logging_enabled) return;

    g_logging_enabled = enabled;
    PersistLoggingPreference(enabled);

    if (enabled)
    {
        // Fresh log per enable - reflects what's happened since the user turned it on.
        g_log_file.open(LogFilePath(), std::ios::out | std::ios::trunc);
    }
    else
    {
        g_log_file.close();
    }
}

bool IsFileLoggingEnabled()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_logging_enabled;
}

void ApplyPersistedLoggingPreference()
{
    SetLoggingEnabled(LoadLoggingPreference());
}

void Log(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (!g_logging_enabled) return;

    g_log_file << "[" << Timestamp() << "] " << line << std::endl;
}
