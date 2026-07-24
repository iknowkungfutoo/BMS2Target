// BMS2Target.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN

#include <csignal>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>

#include "target.h"
#include "FlightData.h"
#include "IVibeData.h"
#include "TrayApp.h"
#include "Logging.h"
#include "DarkMode.h"

const char* VERSION = "2.0.0";

// Integer values (not strings) - the tag-value protocol needs the actual
// numeric value to compare and zero-pad, not a pre-formatted character.
const int LED_STATE_OFF   = 0;
const int LED_STATE_ON    = 1;
const int LED_STATE_FLASH = 8;

// Sentinel for a tag-value "last sent" tracker meaning "never sent yet this
// flight" - so the first update after a flight starts always sends every
// field once, regardless of its actual value.
const int NOT_YET_SENT = -1;

// How often to send the 'h' heartbeat packet (see WIRE_PROTOCOL.md's
// "Heartbeat / connection-loss detection" section) - independent of
// whether a flight/update is happening, so TMHotasLEDSync can tell
// "nothing changed" apart from "this app is gone" even during a long
// idle period at the BMS menu.
const int HEARTBEAT_INTERVAL_MS = 1000;

// Appends a tag-value entry (see WIRE_PROTOCOL.md's packet grammar) to
// `message` only if `value` differs from `*sent_value` (the value last
// actually sent for this field - distinct from the raw shared-memory-bit
// trackers used elsewhere for console printing, since some wire fields are
// derived from more than one raw bit and need their own diff). Updates
// `*sent_value` when it appends. Returns true if it appended anything.
bool append_tag_if_changed(std::string& message, const char* tag, int value, int* sent_value, int width)
{
    if (*sent_value == value) return false;

    *sent_value = value;

    std::string value_str = std::to_string(value);
    size_t padding = width - value_str.length();
    message += tag;
    message += std::to_string(width);
    message += std::string(padding, '0') + value_str;

    return true;
}

namespace
{
    volatile sig_atomic_t quit;

    void signal_handler(int sig)
    {
        signal(sig, signal_handler);
        quit = 1;
    }

    // IsExitGame (used for bms_alive elsewhere) is BMS's own cooperative
    // "I'm quitting" flag - it's never set on a hard crash, so that alone
    // can't detect one. This finds the actual Falcon BMS process by name
    // instead, which the OS always knows the true state of regardless of
    // how it went away. Returns 0 if not found (BMS not running, or a
    // future BMS version renames its executable).
    DWORD FindFalconBmsProcessId()
    {
        DWORD pid = 0;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"Falcon BMS.exe") == 0)
                {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pid;
    }

    // True if the Falcon BMS process is confirmed still running. Owns
    // bms_process_handle: reuses it while it's still valid (a cheap
    // WaitForSingleObject check), lets go of it once BMS exits, and
    // re-searches for a fresh process (covers BMS being restarted) -
    // throttled to ~once/sec via last_search_attempt, since
    // CreateToolhelp32Snapshot enumerates every process on the system and
    // is too expensive to call at the 100ms tick rate while waiting.
    bool IsFalconBmsProcessAlive(HANDLE& bms_process_handle, ULONGLONG& last_search_attempt)
    {
        if (bms_process_handle != NULL)
        {
            if (WaitForSingleObject(bms_process_handle, 0) == WAIT_TIMEOUT)
            {
                return true;
            }

            CloseHandle(bms_process_handle);
            bms_process_handle = NULL;
        }

        ULONGLONG now = GetTickCount64();
        if (now - last_search_attempt < 1000) return false;
        last_search_attempt = now;

        DWORD pid = FindFalconBmsProcessId();
        if (pid == 0) return false;

        bms_process_handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
        return bms_process_handle != NULL;
    }

    // Everything that used to run directly in main() now runs here, on a
    // worker thread, so the main thread stays free to pump the tray icon's
    // Win32 message loop. Returns (instead of running forever) wherever the
    // original code blocked indefinitely, so the tray "Exit" command stays
    // responsive at every stage - waiting for TARGET, waiting for BMS, or
    // mid-flight.
    void RunBmsPollingLoop()
    {
        Log(std::string("BMS2Target v") + VERSION);

        // BMS is waited for first - this app never opens a TARGET
        // connection without a live BMS behind it (see the loop below and
        // the bms_alive handling further down), so TARGET's listener stays
        // free for another exporter (e.g. dcs2target) until BMS actually
        // shows up here.
        Log("Waiting for Falcon BMS...");
        UpdateTrayTooltip(L"BMS2Target - waiting for Falcon BMS...");

        Target target;
        bool target_connected = false;

        HANDLE hFalconSharedMemoryAreaMapFile = NULL;
        HANDLE hFalconSharedMemoryArea2MapFile = NULL;
        HANDLE hFalconIntellivibeSharedMemoryArea = NULL;

        // Shared with the reconnect logic further down (same handle, same
        // throttle) - declared here because the shared-memory mappings
        // alone aren't proof BMS is actually running: Windows only destroys
        // a named section once every handle to it closes, so a stale handle
        // left open by something else (a debugger, a leftover test harness)
        // can keep these mappings valid long after the real Falcon BMS.exe
        // process is gone. Confirmed live (2026-07-24): with only the
        // OpenFileMapping checks below, BMS2Target connected to a running
        // TARGET script with no Falcon BMS process alive at all.
        HANDLE bms_process_handle = NULL;
        ULONGLONG last_bms_process_search = 0;

        bool prev_target_connected = false;
        bool prev_bms_ready = false;

        while (true)
        {
            bool bms_process_alive = IsFalconBmsProcessAlive(bms_process_handle, last_bms_process_search);

            bool bms_ready = bms_process_alive
                           && hFalconSharedMemoryAreaMapFile != NULL
                           && hFalconSharedMemoryArea2MapFile != NULL
                           && hFalconIntellivibeSharedMemoryArea != NULL;

            if (target_connected && bms_ready) break;

            if (quit)
            {
                if (target_connected) target.break_connection();
                if (bms_process_handle != NULL) CloseHandle(bms_process_handle);
                return;
            }

            if (hFalconSharedMemoryAreaMapFile == NULL)
            {
                hFalconSharedMemoryAreaMapFile = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconSharedMemoryArea");
            }
            if (hFalconSharedMemoryArea2MapFile == NULL)
            {
                hFalconSharedMemoryArea2MapFile = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconSharedMemoryArea2");
            }
            if (hFalconIntellivibeSharedMemoryArea == NULL)
            {
                hFalconIntellivibeSharedMemoryArea = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconIntellivibeSharedMemoryArea");
            }

            bms_ready = bms_process_alive
                      && hFalconSharedMemoryAreaMapFile != NULL
                      && hFalconSharedMemoryArea2MapFile != NULL
                      && hFalconIntellivibeSharedMemoryArea != NULL;

            // Never open a TARGET connection without BMS already present -
            // otherwise this app would sit occupying TARGET's listener
            // while it has nothing to send, blocking another exporter
            // (e.g. dcs2target) from connecting in its place. Checked
            // after the OpenFileMapping calls above so a connection is
            // attempted the same iteration BMS is first detected, not one
            // tick later.
            if (!target_connected && bms_ready && target.create_connection())
            {
                target_connected = true;

                // "B" is the exporter type code TMHotasLEDSync uses to
                // recognize BMS2Target specifically - see WIRE_PROTOCOL.md's
                // "Version handshake" section. Only the bare version number
                // follows, not descriptive text.
                std::string version_message("vB" + std::string(VERSION));
                target.send_message(version_message);
            }

            if (target_connected != prev_target_connected || bms_ready != prev_bms_ready)
            {
                // Rising-edge checks against the *old* state, before it gets
                // overwritten below - each balloon fires exactly once, the
                // moment its own condition first becomes true, independent
                // of the other one.
                if (target_connected && !prev_target_connected)
                {
                    Log("Connected to T.A.R.G.E.T.");
                    ShowTrayBalloon(L"BMS2Target", L"Connected to T.A.R.G.E.T.");
                }
                if (bms_ready && !prev_bms_ready)
                {
                    Log("Detected Falcon BMS");
                    ShowTrayBalloon(L"BMS2Target", L"Detected Falcon BMS");
                }

                prev_target_connected = target_connected;
                prev_bms_ready = bms_ready;

                // "connected to T.A.R.G.E.T. but BMS not ready" is no
                // longer reachable - the create_connection() call above
                // only ever runs once bms_ready is already true.
                if (target_connected && bms_ready) UpdateTrayTooltip(L"BMS2Target - connected, waiting for flight...");
                else if (bms_ready)                UpdateTrayTooltip(L"BMS2Target - detected Falcon BMS, connecting to T.A.R.G.E.T...");
                else                                UpdateTrayTooltip(L"BMS2Target - waiting for Falcon BMS...");
            }

            // Heartbeat, once per iteration - this loop already ticks at
            // ~1s via the Sleep(1000) below, so no separate timer is
            // needed here.
            if (target_connected)
            {
                if (!target.send_message("h")) target_connected = false;
            }

            Sleep(1000);
        }

        const FlightData* flightdata = (FlightData*)MapViewOfFile(hFalconSharedMemoryAreaMapFile, FILE_MAP_READ, 0, 0, sizeof(FlightData));
        const FlightData2* flightdata2 = (FlightData2*)MapViewOfFile(hFalconSharedMemoryArea2MapFile, FILE_MAP_READ, 0, 0, sizeof(FlightData2));
        const IntellivibeData* intelliVibeData = (IntellivibeData*)MapViewOfFile(hFalconIntellivibeSharedMemoryArea, FILE_MAP_READ, 0, 0, sizeof(IntellivibeData));

        // Both are ready now (the loop above only exits once they are) - the
        // "just connected"/"just detected" balloons already fired in there,
        // whichever order they happened in.
        Log("Both T.A.R.G.E.T. and Falcon BMS are ready");
        UpdateTrayTooltip(L"BMS2Target - connected, waiting for flight...");

        std::string aircraft_message("mF-16C_50");
        target.send_message(aircraft_message);

        if (flightdata != NULL && flightdata2 != NULL)
        {
        unsigned int nose_gear_down_lamp_state   = 0xFFFFFFFF;
        unsigned int left_gear_down_lamp_state   = 0xFFFFFFFF;
        unsigned int right_gear_down_lamp_state  = 0xFFFFFFFF;
        unsigned int gear_handle_lamp_state      = 0xFFFFFFFF;
        unsigned int aux_search_lamp_state       = 0xFFFFFFFF;
        unsigned int aux_search_lamp_flash_state = 0xFFFFFFFF;
        unsigned int aux_act_lamp_state          = 0xFFFFFFFF;
        unsigned int aux_low_lamp_state          = 0xFFFFFFFF;
        unsigned int aux_power_lamp_state        = 0xFFFFFFFF;

        unsigned int jfs_run_lamp_state          = 0xFFFFFFFF;
        unsigned int main_gen_lamp_state         = 0xFFFFFFFF;
        unsigned int stby_gen_lamp_status        = 0xFFFFFFFF;
        unsigned int flcs_rly_lamp_status        = 0xFFFFFFFF;
        unsigned int epu_lamp_status             = 0xFFFFFFFF;

        unsigned int speed_brake_position = 100;

        // Last value actually SENT over the wire for each tag-value field
        // (see WIRE_PROTOCOL.md) - NOT_YET_SENT means "never sent", so the
        // first update of a flight always sends every field once. Distinct
        // from the raw lamp-state trackers above: the RWR sub-fields are
        // derived from aux_power_lamp_state gating several raw bits at once,
        // so they need their own diff against what was actually sent, not
        // just against their own raw bit.
        int sent_nose_gear      = NOT_YET_SENT;
        int sent_left_gear      = NOT_YET_SENT;
        int sent_right_gear     = NOT_YET_SENT;
        int sent_gear_warning   = NOT_YET_SENT;
        int sent_rwr_search     = NOT_YET_SENT;
        int sent_rwr_activity   = NOT_YET_SENT;
        int sent_rwr_act_power  = NOT_YET_SENT;
        int sent_rwr_alt_low    = NOT_YET_SENT;
        int sent_rwr_alt        = NOT_YET_SENT;
        int sent_rwr_power      = NOT_YET_SENT;
        int sent_jfs_run        = NOT_YET_SENT;
        int sent_main_gen       = NOT_YET_SENT;
        int sent_stby_gen       = NOT_YET_SENT;
        int sent_flcs_rly       = NOT_YET_SENT;
        int sent_epu            = NOT_YET_SENT;
        int sent_speed_brake    = NOT_YET_SENT;

        bool flight_ended = false;
        bool prev_in3d = false;
        bool prev_bms_alive = true;
        bool force_full_resync = false;
        ULONGLONG last_target_reconnect_attempt = 0;
        ULONGLONG last_heartbeat_sent = 0;

        // Set whenever we disconnect because BMS went away. BMS's shutdown
        // isn't instantaneous - it sets IsExitGame (triggering our
        // disconnect) before the OS process actually terminates, so
        // bms_process_alive can keep reading true for a bit afterward,
        // still referring to the very process that just told us it's
        // leaving. Blocks reconnecting on that stale signal until we've
        // actually observed this process finish exiting.
        bool waiting_for_bms_process_exit = false;

        // True once IsExitGame can be trusted. Starts true: this is the
        // first time this session has ever mapped this shared memory, so
        // there's no prior session's data for it to be stale from - a
        // freshly created mapping zero-inits to IsExitGame == false, which
        // is already the correct answer. Reset false specifically on every
        // *reconnect* (not here) - that's the case with a real staleness
        // risk, since our own open handle can keep the shared-memory
        // object alive across BMS sessions, letting a new BMS instance
        // attach to one still holding the *previous* session's leftover
        // content (e.g. IsExitGame stuck true from a prior exit) until an
        // actual flight starts and BMS writes fresh data - see
        // bms_process_alive's comment. Confirmed live (2026-07-21) in two
        // different ways: without ever setting this true post-reconnect,
        // exiting BMS a second time without flying in between never got
        // detected at all (bms_alive stuck reading false, nothing to
        // transition away from); and initializing this to false instead of
        // true made the very first, perfectly legitimate exit of a session
        // that never flew get misreported as "closed unexpectedly" instead
        // of a clean exit, since IsExitGame was being distrusted when there
        // was nothing stale for it to be distrusted *from*.
        bool bms_data_fresh = true;

        unsigned int value;
        bool updated;

        while (!quit)
        {
            Sleep(100);

            // bms_process_alive is the OS's own view of whether Falcon BMS
            // is running - true the instant the process (re)starts,
            // independent of shared-memory content. IntellivibeData
            // (IsExitGame included) isn't refreshed by BMS until you're
            // actually in a flight, so it still reads stale content from
            // the *previous* session for a while after a restart - fine
            // for noticing BMS went away (it correctly flips once BMS sets
            // it on exit), but useless for noticing BMS came back quickly.
            // Confirmed live (2026-07-21): gating the reconnect below on
            // the combined bms_alive delayed reconnection until flight
            // start instead of as soon as BMS relaunched.
            bool bms_process_alive = IsFalconBmsProcessAlive(bms_process_handle, last_bms_process_search);

            if (waiting_for_bms_process_exit && !bms_process_alive)
            {
                // The process we disconnected from has now genuinely
                // terminated (not just told us it's exiting) - any process
                // detected from here on is a new/different instance, safe
                // to reconnect to.
                waiting_for_bms_process_exit = false;
            }

            if (intelliVibeData->In3D) bms_data_fresh = true;

            // IsExitGame alone only catches a graceful BMS exit - it's
            // never set on a hard crash. Also checking the actual OS
            // process catches both: either signal going false marks BMS
            // as gone. Used for the liveness *transition* (disconnect/'q')
            // and lamp-processing gate below - NOT for the reconnect gate,
            // which needs bms_process_alive alone (see above). IsExitGame
            // is only trusted once bms_data_fresh - otherwise it can still
            // be stale content from the previous session (see that
            // variable's declaration comment), so a process that's
            // genuinely running is assumed alive until proven otherwise.
            bool bms_alive = bms_process_alive && (!bms_data_fresh || !intelliVibeData->IsExitGame);

            // --- TARGET reconnection, throttled to ~once/sec so a closed
            // port doesn't get hammered 10x/second at the 100ms tick rate.
            // Gated on bms_process_alive (not the combined bms_alive) so
            // reconnection happens as soon as BMS itself restarts, not
            // whenever it next refreshes IntellivibeData (which can be as
            // late as the next flight start) - see the comment above. Also
            // gated on !waiting_for_bms_process_exit so a disconnect
            // doesn't immediately reconnect to the tail end of the same
            // still-shutting-down BMS process it just disconnected from.
            // Same reasoning as the initial connection (in the startup
            // loop, above RunBmsPollingLoop's main loop) waiting for BMS
            // first: don't occupy TARGET's listener with nothing to send,
            // so another exporter (e.g. dcs2target) can use it instead if
            // the user has switched sims.
            if (!target_connected && bms_process_alive && !waiting_for_bms_process_exit)
            {
                ULONGLONG now = GetTickCount64();
                if (now - last_target_reconnect_attempt >= 1000)
                {
                    last_target_reconnect_attempt = now;

                    if (target.create_connection())
                    {
                        target_connected = true;

                        // This connection hasn't observed a flight start
                        // yet, so IsExitGame can't be trusted until it
                        // does - see bms_data_fresh's declaration comment.
                        bms_data_fresh = false;

                        Log("Reconnected to T.A.R.G.E.T.");
                        ShowTrayBalloon(L"BMS2Target", L"Reconnected to T.A.R.G.E.T.");

                        // Resend the handshake and aircraft type unconditionally -
                        // covers both "our socket dropped" and "the TARGET script
                        // itself restarted and forgot the aircraft type", which
                        // look identical from here.
                        std::string version_message("vB" + std::string(VERSION));
                        target.send_message(version_message);

                        std::string aircraft_message("mF-16C_50");
                        target.send_message(aircraft_message);

                        // Force a full resync - this connection hasn't seen any
                        // of the deltas we'd otherwise only send on change.
                        sent_nose_gear      = NOT_YET_SENT;
                        sent_left_gear      = NOT_YET_SENT;
                        sent_right_gear     = NOT_YET_SENT;
                        sent_gear_warning   = NOT_YET_SENT;
                        sent_rwr_search     = NOT_YET_SENT;
                        sent_rwr_activity   = NOT_YET_SENT;
                        sent_rwr_act_power  = NOT_YET_SENT;
                        sent_rwr_alt_low    = NOT_YET_SENT;
                        sent_rwr_alt        = NOT_YET_SENT;
                        sent_rwr_power      = NOT_YET_SENT;
                        sent_jfs_run        = NOT_YET_SENT;
                        sent_main_gen       = NOT_YET_SENT;
                        sent_stby_gen       = NOT_YET_SENT;
                        sent_flcs_rly       = NOT_YET_SENT;
                        sent_epu            = NOT_YET_SENT;
                        sent_speed_brake    = NOT_YET_SENT;

                        // Resetting sent_* trackers alone doesn't force a send
                        // this tick - the send is gated by `updated`, which
                        // only goes true when a raw value actually changes.
                        // If nothing happens to change on this exact tick, the
                        // resync would otherwise sit dormant until the next
                        // incidental lamp change (which might be a long time).
                        force_full_resync = true;

                        // The combined bms_alive (IsExitGame-based) may not
                        // have caught up yet even though the OS process is
                        // confirmed alive - see the bms_process_alive
                        // comment above - so this is optimistic, not a
                        // guarantee bms_alive is true yet too.
                        UpdateTrayTooltip(L"BMS2Target - connected, waiting for flight...");
                    }
                }
            }

            // --- Heartbeat, so TMHotasLEDSync can tell "nothing changed"
            // apart from "this app died" even though updates are
            // delta-only - see WIRE_PROTOCOL.md's heartbeat section. Sent
            // independent of BMS/flight state, as long as TARGET is
            // connected.
            if (target_connected)
            {
                ULONGLONG now = GetTickCount64();
                if (now - last_heartbeat_sent >= HEARTBEAT_INTERVAL_MS)
                {
                    last_heartbeat_sent = now;
                    if (!target.send_message("h")) target_connected = false;
                }
            }

            // --- BMS liveness transition. Deliberately not distinguishing
            // graceful vs unexpected here anymore - see git history/memory
            // for the false starts (IsExitGame-based, then exit-code-based)
            // that got scrapped: it's self-evident to the user whether they
            // closed BMS themselves or it crashed, so a single neutral
            // message covers both without risking a misleading "graceful"
            // or "unexpected" label on the wrong case.
            if (bms_alive != prev_bms_alive)
            {
                if (!bms_alive)
                {
                    Log("Falcon BMS closed");
                    UpdateTrayTooltip(L"BMS2Target - waiting for Falcon BMS...");

                    // Block reconnecting until we've confirmed this
                    // specific process has actually finished exiting, not
                    // just that IsExitGame said it's leaving - see the
                    // waiting_for_bms_process_exit declaration comment.
                    waiting_for_bms_process_exit = true;

                    if (target_connected)
                    {
                        // "q" (simulation exit) maps to TMHotasLEDSync's
                        // lightweight lights_out() - see WIRE_PROTOCOL.md.
                        // Best-effort: even if this doesn't arrive,
                        // TMHotasLEDSync's own heartbeat watchdog darkens
                        // the LEDs within a few seconds once this app
                        // disconnects below, so nothing is lost either way.
                        target.send_message("q");

                        // Disconnect entirely rather than idling
                        // connected-but-silent - never hold TARGET's
                        // listener open without a live BMS behind it, so
                        // another exporter (e.g. dcs2target) can take the
                        // port instead if the user switches sims without
                        // restarting TMHotasLEDSync.
                        target.break_connection();
                        target_connected = false;
                    }
                }
                else
                {
                    // By the time IsExitGame catches up (see the
                    // bms_process_alive comment above), the reconnect
                    // block has usually already reconnected via
                    // bms_process_alive alone - reflect whichever is
                    // actually true rather than assuming "still connecting".
                    Log("Falcon BMS is responding again");
                    UpdateTrayTooltip(target_connected
                        ? L"BMS2Target - connected, waiting for flight..."
                        : L"BMS2Target - Falcon BMS detected, connecting to T.A.R.G.E.T...");
                }

                prev_bms_alive = bms_alive;
            }

            if (!bms_alive)
            {
                // Nothing else to do this tick - don't touch lamp state or
                // sent_* trackers while BMS isn't there to provide it.
                continue;
            }

            if (intelliVibeData->In3D && !prev_in3d)
            {
                // flight starting... reset all state variables
                nose_gear_down_lamp_state   = 0xFFFFFFFF;
                left_gear_down_lamp_state   = 0xFFFFFFFF;
                right_gear_down_lamp_state  = 0xFFFFFFFF;
                gear_handle_lamp_state      = 0xFFFFFFFF;
                aux_search_lamp_state       = 0xFFFFFFFF;
                aux_search_lamp_flash_state = 0xFFFFFFFF;
                aux_act_lamp_state          = 0xFFFFFFFF;
                aux_low_lamp_state          = 0xFFFFFFFF;
                aux_power_lamp_state        = 0xFFFFFFFF;

                jfs_run_lamp_state          = 0xFFFFFFFF;
                main_gen_lamp_state         = 0xFFFFFFFF;
                stby_gen_lamp_status        = 0xFFFFFFFF;
                flcs_rly_lamp_status        = 0xFFFFFFFF;
                epu_lamp_status             = 0xFFFFFFFF;

                speed_brake_position = 100;

                sent_nose_gear      = NOT_YET_SENT;
                sent_left_gear      = NOT_YET_SENT;
                sent_right_gear     = NOT_YET_SENT;
                sent_gear_warning   = NOT_YET_SENT;
                sent_rwr_search     = NOT_YET_SENT;
                sent_rwr_activity   = NOT_YET_SENT;
                sent_rwr_act_power  = NOT_YET_SENT;
                sent_rwr_alt_low    = NOT_YET_SENT;
                sent_rwr_alt        = NOT_YET_SENT;
                sent_rwr_power      = NOT_YET_SENT;
                sent_jfs_run        = NOT_YET_SENT;
                sent_main_gen       = NOT_YET_SENT;
                sent_stby_gen       = NOT_YET_SENT;
                sent_flcs_rly       = NOT_YET_SENT;
                sent_epu            = NOT_YET_SENT;
                sent_speed_brake    = NOT_YET_SENT;

                flight_ended = false;

                Log("Start flight!");
                UpdateTrayTooltip(L"BMS2Target - in flight");
                ShowTrayBalloon(L"BMS2Target", L"Start flight!");
            }
            prev_in3d = intelliVibeData->In3D;

            if (intelliVibeData->In3D)
            {
                updated = force_full_resync;
                force_full_resync = false;

                if (intelliVibeData->IsEndFlight && !flight_ended)
                {
                    flight_ended = true;

                    Log("End flight!");
                    UpdateTrayTooltip(L"BMS2Target - connected, waiting for flight...");
                    ShowTrayBalloon(L"BMS2Target", L"End flight!");
                    if (target_connected && !target.send_message("r")) target_connected = false;
                }

                value = flightdata->lightBits3 & FlightData::NoseGearDown;
                if (nose_gear_down_lamp_state != value)
                {
                    nose_gear_down_lamp_state = value;
                    updated = true;

                    if (nose_gear_down_lamp_state == FlightData::NoseGearDown)
                    {
                        Log("Nose Gear: Down");
                    }
                    else
                    {
                        Log("Nose Gear: Up");
                    }
                }

                value = flightdata->lightBits3 & FlightData::LeftGearDown;
                if (left_gear_down_lamp_state != value)
                {
                    left_gear_down_lamp_state = value;
                    updated = true;

                    if (left_gear_down_lamp_state == FlightData::LeftGearDown)
                    {
                        Log("Left Gear: Down");
                    }
                    else
                    {
                        Log("Left Gear: Up");
                    }
                }

                value = flightdata->lightBits3 & FlightData::RightGearDown;
                if (right_gear_down_lamp_state != value)
                {
                    right_gear_down_lamp_state = value;
                    updated = true;

                    if (right_gear_down_lamp_state == FlightData::RightGearDown)
                    {
                        Log("Right Gear: Down");
                    }
                    else
                    {
                        Log("Right Gear: Up");
                    }
                }

                value = flightdata->lightBits2 & FlightData::GEARHANDLE;
                if (gear_handle_lamp_state != value)
                {
                    gear_handle_lamp_state = value;
                    updated = true;

                    if (gear_handle_lamp_state == FlightData::GEARHANDLE)
                    {
                        Log("Gear Warning: On");
                    }
                    else
                    {
                        Log("Gear Warning: Off");
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxSrch;
                if (aux_search_lamp_state != value)
                {
                    aux_search_lamp_state = value;
                    updated = true;

                    if (aux_search_lamp_state == FlightData::AuxSrch)
                    {
                        Log("Aux Threat Warning Search: On");
                    }
                    else
                    {
                        Log("Aux Threat Warning Search: Off");
                    }
                }

                value = flightdata2->blinkBits & FlightData2::AuxSrch;
                if (aux_search_lamp_flash_state != value)
                {
                    aux_search_lamp_flash_state = value;
                    updated = true;

                    if (aux_search_lamp_flash_state & FlightData2::AuxSrch)
                    {
                        Log("Aux Threat Warning Search: Flashing");
                    }
                    else
                    {
                        Log("Aux Threat Warning Search: Steady");
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxAct;
                if (aux_act_lamp_state != value)
                {
                    aux_act_lamp_state = value;
                    updated = true;

                    if (aux_act_lamp_state == FlightData::AuxAct)
                    {
                        Log("Aux Threat Warning Activity: On");
                    }
                    else
                    {
                        Log("Aux Threat Warning Activity: Off");
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxLow;
                if (aux_low_lamp_state != value)
                {
                    aux_low_lamp_state = value;
                    updated = true;

                    if (aux_low_lamp_state == FlightData::AuxLow)
                    {
                        Log("Aux Threat Warning Low: On");
                    }
                    else
                    {
                        Log("Aux Threat Warning Low: Off");
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxPwr;
                if (aux_power_lamp_state != value)
                {
                    aux_power_lamp_state = value;
                    updated = true;

                    if (aux_power_lamp_state == FlightData::AuxPwr)
                    {
                        Log("Aux Threat Warning Power: On");
                    }
                    else
                    {
                        Log("Aux Threat Warning Power: Off");
                    }
                }

                value = flightdata->lightBits2 & FlightData::JFSOn;
                if (jfs_run_lamp_state != value)
                {
                    jfs_run_lamp_state = value;
                    updated = true;

                    if (jfs_run_lamp_state == FlightData::JFSOn)
                    {
                        Log("JFS: On");
                    }
                    else
                    {
                        Log("JFS: Off");
                    }
                }

                value = flightdata->lightBits3 & FlightData::MainGen;
                if (main_gen_lamp_state != value)
                {
                    main_gen_lamp_state = value;
                    updated = true;

                    if (main_gen_lamp_state == FlightData::MainGen)
                    {
                        Log("MainGen: On");
                    }
                    else
                    {
                        Log("MainGen: Off");
                    }
                }

                value = flightdata->lightBits3 & FlightData::StbyGen;
                if (stby_gen_lamp_status != value)
                {
                    stby_gen_lamp_status = value;
                    updated = true;

                    if (stby_gen_lamp_status == FlightData::StbyGen)
                    {
                        Log("StbyGen: On");
                    }
                    else
                    {
                        Log("StbyGen: Off");
                    }
                }

                value = flightdata->lightBits3 & FlightData::FlcsRly;
                if (flcs_rly_lamp_status != value)
                {
                    flcs_rly_lamp_status = value;
                    updated = true;

                    if (flcs_rly_lamp_status == FlightData::FlcsRly)
                    {
                        Log("FlcsRly: On");
                    }
                    else
                    {
                        Log("FlcsRly: Off");
                    }
                }

                value = flightdata->lightBits2 & FlightData::EPUOn;
                if (epu_lamp_status != value)
                {
                    epu_lamp_status = value;
                    updated = true;

                    if (epu_lamp_status == FlightData::EPUOn)
                    {
                        Log("EPUOn: On");
                    }
                    else
                    {
                        Log("EPUOn: Off");
                    }
                }

                value = (unsigned int)(flightdata->speedBrake * 100); // convert to percentage, i.e. 0.0 .. 1.0 to 0 .. 100

                // convert to five steps (plus off) to correspond to the five user LEDs on Thrustmaster throttles
                if      (value >= 90) value = 100;
                else if (value >= 70) value = 80;
                else if (value >= 50) value = 60;
                else if (value >= 30) value = 40;
                else if (value >= 10) value = 20;
                else value = 0;

                if (speed_brake_position != value)
                {
                    speed_brake_position = value;
                    updated = true;

                    Log("Speed brake position: " + std::to_string(value));
                }

                if (updated)
                {
                    // Tag-value protocol (see WIRE_PROTOCOL.md) - only fields
                    // that actually changed get appended. The RWR sub-fields
                    // below are all derived from aux_power_lamp_state gating
                    // several raw bits at once, so each is diffed against its
                    // own sent_* tracker (via append_tag_if_changed), not
                    // against `updated` (which just means "something
                    // happened this tick", not "this specific tag needs
                    // resending").
                    std::string message("u");
                    bool any_changed = false;

                    int gear_nose    = (nose_gear_down_lamp_state  == FlightData::NoseGearDown)  ? LED_STATE_ON : LED_STATE_OFF;
                    int gear_left    = (left_gear_down_lamp_state  == FlightData::LeftGearDown)  ? LED_STATE_ON : LED_STATE_OFF;
                    int gear_right   = (right_gear_down_lamp_state == FlightData::RightGearDown) ? LED_STATE_ON : LED_STATE_OFF;
                    int gear_warning = (gear_handle_lamp_state     == FlightData::GEARHANDLE)    ? LED_STATE_ON : LED_STATE_OFF;

                    any_changed |= append_tag_if_changed(message, "N", gear_nose,    &sent_nose_gear,    1);
                    any_changed |= append_tag_if_changed(message, "L", gear_left,    &sent_left_gear,    1);
                    any_changed |= append_tag_if_changed(message, "R", gear_right,   &sent_right_gear,   1);
                    any_changed |= append_tag_if_changed(message, "W", gear_warning, &sent_gear_warning, 1);

                    int rwr_search, rwr_activity, rwr_act_power, rwr_alt_low, rwr_alt, rwr_power;

                    if (aux_power_lamp_state & FlightData::AuxPwr)
                    {
                        if (aux_search_lamp_state & FlightData::AuxSrch)
                        {
                            rwr_search = ((aux_search_lamp_flash_state & FlightData2::AuxSrch) == FlightData2::AuxSrch) ? LED_STATE_FLASH : LED_STATE_ON;
                        }
                        else
                        {
                            rwr_search = LED_STATE_OFF;
                        }

                        // rwr_activity combines with rwr_act_power on the
                        // TMHotasLEDSync side to flash the ACT/PWR LED when
                        // activity is on, solid when only power is on.
                        rwr_activity  = (aux_act_lamp_state   == FlightData::AuxAct) ? LED_STATE_ON : LED_STATE_OFF;
                        rwr_act_power = (aux_power_lamp_state == FlightData::AuxPwr) ? LED_STATE_ON : LED_STATE_OFF;
                        rwr_alt_low   = (aux_low_lamp_state   == FlightData::AuxLow) ? LED_STATE_ON : LED_STATE_OFF;
                        // rwr_alt reuses aux_power_lamp_state - shared memory
                        // has no separate "normal altitude" bit, so this
                        // shows green ALT whenever powered and not low-alt.
                        rwr_alt       = (aux_power_lamp_state == FlightData::AuxPwr) ? LED_STATE_ON : LED_STATE_OFF;
                        rwr_power     = LED_STATE_ON;
                    }
                    else
                    {
                        rwr_search    = LED_STATE_OFF;
                        rwr_activity  = LED_STATE_OFF;
                        rwr_act_power = LED_STATE_OFF;
                        rwr_alt_low   = LED_STATE_OFF;
                        rwr_alt       = LED_STATE_OFF;
                        rwr_power     = LED_STATE_OFF;
                    }

                    any_changed |= append_tag_if_changed(message, "Q", rwr_search,    &sent_rwr_search,    1);
                    any_changed |= append_tag_if_changed(message, "A", rwr_activity,  &sent_rwr_activity,  1);
                    any_changed |= append_tag_if_changed(message, "Z", rwr_act_power, &sent_rwr_act_power, 1);
                    any_changed |= append_tag_if_changed(message, "J", rwr_alt_low,   &sent_rwr_alt_low,   1);
                    any_changed |= append_tag_if_changed(message, "E", rwr_alt,       &sent_rwr_alt,       1);
                    any_changed |= append_tag_if_changed(message, "V", rwr_power,     &sent_rwr_power,     1);

                    int jfs_run  = (jfs_run_lamp_state   == FlightData::JFSOn)   ? LED_STATE_ON : LED_STATE_OFF;
                    int main_gen = (main_gen_lamp_state  == FlightData::MainGen) ? LED_STATE_ON : LED_STATE_OFF;
                    int stby_gen = (stby_gen_lamp_status == FlightData::StbyGen) ? LED_STATE_ON : LED_STATE_OFF;
                    int flcs_rly = (flcs_rly_lamp_status == FlightData::FlcsRly) ? LED_STATE_ON : LED_STATE_OFF;
                    int epu      = (epu_lamp_status      == FlightData::EPUOn)   ? LED_STATE_ON : LED_STATE_OFF;

                    any_changed |= append_tag_if_changed(message, "S", jfs_run,  &sent_jfs_run,  1);
                    any_changed |= append_tag_if_changed(message, "G", main_gen, &sent_main_gen, 1);
                    any_changed |= append_tag_if_changed(message, "T", stby_gen, &sent_stby_gen, 1);
                    any_changed |= append_tag_if_changed(message, "C", flcs_rly, &sent_flcs_rly, 1);
                    any_changed |= append_tag_if_changed(message, "U", epu,      &sent_epu,      1);

                    // Speed Brake position
                    any_changed |= append_tag_if_changed(message, "B", (int)speed_brake_position, &sent_speed_brake, 3);

                    if (any_changed && target_connected)
                    {
                        if (!target.send_message(message)) target_connected = false;
                    }
                }
            }
        }

        if (bms_process_handle != NULL) CloseHandle(bms_process_handle);
    }

        if (quit && target_connected) target.send_message("r");

        UnmapViewOfFile(flightdata);
        UnmapViewOfFile(flightdata2);
        UnmapViewOfFile(intelliVibeData);

        CloseHandle(hFalconSharedMemoryAreaMapFile);
        CloseHandle(hFalconSharedMemoryArea2MapFile);
        CloseHandle(hFalconIntellivibeSharedMemoryArea);

        target.break_connection();
    }
}

int main()
{
    // Process-wide, not tied to any window - safe to call before anything
    // else, including the single-instance message box below.
    InitDarkModeSupport();

    // A silently-running tray app is much easier to accidentally launch
    // twice than a console app - two copies would each open their own TCP
    // connection to TARGET and race each other. Refuse to start a second
    // instance instead.
    HANDLE single_instance_mutex = CreateMutexW(NULL, TRUE, L"BMS2Target_SingleInstanceMutex");
    if (single_instance_mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ShowThemedMessageBox(NULL, L"BMS2Target is already running (check the system tray).", L"BMS2Target");
        return EXIT_FAILURE;
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGBREAK
    signal(SIGBREAK, signal_handler);
#endif

    std::string version_str(VERSION);
    std::wstring app_title = L"BMS2Target v" + std::wstring(version_str.begin(), version_str.end());
    if (!CreateTrayApp(app_title))
    {
        return EXIT_FAILURE;
    }

    SetTrayExitCallback([]() { quit = 1; });
    SetTrayLoggingToggleCallback([](bool enabled) { SetLoggingEnabled(enabled); });

    // Before the worker thread starts (and long before the user could open
    // the tray menu), so logging is already active for the whole session if
    // it was left enabled last time - the toggle used to always reset to
    // off on restart, which made it easy to miss when trying to capture a
    // log for an intermittent bug.
    ApplyPersistedLoggingPreference();

    std::thread worker(RunBmsPollingLoop);

    int exit_code = RunTrayMessageLoop();

    quit = 1;
    worker.join();

    DestroyTrayApp();

    return exit_code;
}
