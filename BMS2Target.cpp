// BMS2Target.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN

#include <csignal>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <format>

#include "target.h"
#include "FlightData.h"
#include "IVibeData.h"

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
}

int main()
{
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
#ifdef SIGBREAK
    signal(SIGBREAK, signal_handler);
#endif

    std::cout << "BMS2Target v" << VERSION << std::endl << std::endl;

    SOCKET s = INVALID_SOCKET;

    std::cout << "Waiting to connect..." << std::endl;

    Target target;
    while (!target.create_connection())
    {
        Sleep(1000);
    }
    std::cout << "Connected to T.A.R.G.E.T." << std::endl;

    // "B" is the exporter type code TMHotasLEDSync uses to recognize
    // BMS2Target specifically - see WIRE_PROTOCOL.md's "Version handshake"
    // section. Only the bare version number follows, not descriptive text.
    std::string message("vB" + std::string(VERSION));
    target.send_message(message);


    HANDLE hFalconSharedMemoryAreaMapFile = NULL;
    while (hFalconSharedMemoryAreaMapFile == NULL)
    {
        hFalconSharedMemoryAreaMapFile = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconSharedMemoryArea");
        Sleep(1000);
    }
    const FlightData* flightdata = (FlightData*)MapViewOfFile(hFalconSharedMemoryAreaMapFile, FILE_MAP_READ, 0, 0, sizeof(FlightData));

    HANDLE hFalconSharedMemoryArea2MapFile = NULL;
    while (hFalconSharedMemoryArea2MapFile == NULL)
    {
        hFalconSharedMemoryArea2MapFile = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconSharedMemoryArea2");
        Sleep(1000);
    }
    const FlightData2* flightdata2 = (FlightData2*)MapViewOfFile(hFalconSharedMemoryArea2MapFile, FILE_MAP_READ, 0, 0, sizeof(FlightData2));

    HANDLE hFalconIntellivibeSharedMemoryArea = NULL;
    while (hFalconIntellivibeSharedMemoryArea == NULL)
    {
        hFalconIntellivibeSharedMemoryArea = OpenFileMapping(FILE_MAP_READ, FALSE, L"FalconIntellivibeSharedMemoryArea");
        Sleep(1000);
    }
    const IntellivibeData* intelliVibeData = (IntellivibeData*)MapViewOfFile(hFalconIntellivibeSharedMemoryArea, FILE_MAP_READ, 0, 0, sizeof(IntellivibeData));

    std::cout << "Connected to Falcon BMS" << std::endl;


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

        unsigned int value;
        bool updated;

        std::string message("mF-16C_50");
        target.send_message(message);

        while (!quit)
        {
            Sleep(100);

            if (intelliVibeData->In3D)
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

                std::cout << std::endl << "Start flight!";
            }

            while (intelliVibeData->In3D && !quit)
            {
                Sleep(100);

                updated = false;

                if (intelliVibeData->IsEndFlight && !flight_ended)
                {
                    flight_ended = true;

                    std::cout << std::endl << "End flight!";
                    target.send_message("r");
                }

                value = flightdata->lightBits3 & FlightData::NoseGearDown;
                if (nose_gear_down_lamp_state != value)
                {
                    nose_gear_down_lamp_state = value;
                    updated = true;

                    if (nose_gear_down_lamp_state == FlightData::NoseGearDown)
                    {
                        std::cout << std::endl << "Nose Gear: Down";
                    }
                    else
                    {
                        std::cout << std::endl << "Nose Gear: Up";
                    }
                }

                value = flightdata->lightBits3 & FlightData::LeftGearDown;
                if (left_gear_down_lamp_state != value)
                {
                    left_gear_down_lamp_state = value;
                    updated = true;

                    if (left_gear_down_lamp_state == FlightData::LeftGearDown)
                    {
                        std::cout << std::endl << "Left Gear: Down";
                    }
                    else
                    {
                        std::cout << std::endl << "Left Gear: Up";
                    }
                }

                value = flightdata->lightBits3 & FlightData::RightGearDown;
                if (right_gear_down_lamp_state != value)
                {
                    right_gear_down_lamp_state = value;
                    updated = true;

                    if (right_gear_down_lamp_state == FlightData::RightGearDown)
                    {
                        std::cout << std::endl << "Right Gear: Down";
                    }
                    else
                    {
                        std::cout << std::endl << "Right Gear: Up";
                    }
                }

                value = flightdata->lightBits2 & FlightData::GEARHANDLE;
                if (gear_handle_lamp_state != value)
                {
                    gear_handle_lamp_state = value;
                    updated = true;

                    if (gear_handle_lamp_state == FlightData::GEARHANDLE)
                    {
                        std::cout << std::endl << "Gear Warning: On";
                    }
                    else
                    {
                        std::cout << std::endl << "Gear Warning: Off";
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxSrch;
                if (aux_search_lamp_state != value)
                {
                    aux_search_lamp_state = value;
                    updated = true;

                    if (aux_search_lamp_state == FlightData::AuxSrch)
                    {
                        std::cout << std::endl << "Aux Threat Warning Search: On";
                    }
                    else
                    {
                        std::cout << std::endl << "Aux Threat Warning Search: Off";
                    }
                }

                value = flightdata2->blinkBits & FlightData2::AuxSrch;
                if (aux_search_lamp_flash_state != value)
                {
                    aux_search_lamp_flash_state = value;
                    updated = true;

                    if (aux_search_lamp_flash_state & FlightData2::AuxSrch)
                    {
                        std::cout << std::endl << "Aux Threat Warning Search: Flashing";
                    }
                    else
                    {
                        std::cout << std::endl << "Aux Threat Warning Search: Steady";
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxAct;
                if (aux_act_lamp_state != value)
                {
                    aux_act_lamp_state = value;
                    updated = true;

                    if (aux_act_lamp_state == FlightData::AuxAct)
                    {
                        std::cout << std::endl << "Aux Threat Warning Activity: On";
                    }
                    else
                    {
                        std::cout << std::endl << "Aux Threat Warning Activity: Off";
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxLow;
                if (aux_low_lamp_state != value)
                {
                    aux_low_lamp_state = value;
                    updated = true;

                    if (aux_low_lamp_state == FlightData::AuxLow)
                    {
                        std::cout << std::endl << "Aux Threat Warning Low: On";
                    }
                    else
                    {
                        std::cout << std::endl << "Aux Threat Warning Low: Off";
                    }
                }

                value = flightdata->lightBits2 & FlightData::AuxPwr;
                if (aux_power_lamp_state != value)
                {
                    aux_power_lamp_state = value;
                    updated = true;

                    if (aux_power_lamp_state == FlightData::AuxPwr)
                    {
                        std::cout << std::endl << "Aux Threat Warning Power: On";
                    }
                    else
                    {
                        std::cout << std::endl << "Aux Threat Warning Power: Off";
                    }
                }

                value = flightdata->lightBits2 & FlightData::JFSOn;
                if (jfs_run_lamp_state != value)
                {
                    jfs_run_lamp_state = value;
                    updated = true;

                    if (jfs_run_lamp_state == FlightData::JFSOn)
                    {
                        std::cout << std::endl << "JFS: On";
                    }
                    else
                    {
                        std::cout << std::endl << "JFS: Off";
                    }
                }

                value = flightdata->lightBits3 & FlightData::MainGen;
                if (main_gen_lamp_state != value)
                {
                    main_gen_lamp_state = value;
                    updated = true;

                    if (main_gen_lamp_state == FlightData::MainGen)
                    {
                        std::cout << std::endl << "MainGen: On";
                    }
                    else
                    {
                        std::cout << std::endl << "MainGen: Off";
                    }
                }

                value = flightdata->lightBits3 & FlightData::StbyGen;
                if (stby_gen_lamp_status != value)
                {
                    stby_gen_lamp_status = value;
                    updated = true;

                    if (stby_gen_lamp_status == FlightData::StbyGen)
                    {
                        std::cout << std::endl << "StbyGen: On";
                    }
                    else
                    {
                        std::cout << std::endl << "StbyGen: Off";
                    }
                }

                value = flightdata->lightBits3 & FlightData::FlcsRly;
                if (flcs_rly_lamp_status != value)
                {
                    flcs_rly_lamp_status = value;
                    updated = true;

                    if (flcs_rly_lamp_status == FlightData::FlcsRly)
                    {
                        std::cout << std::endl << "FlcsRly: On";
                    }
                    else
                    {
                        std::cout << std::endl << "FlcsRly: Off";
                    }
                }

                value = flightdata->lightBits2 & FlightData::EPUOn;
                if (epu_lamp_status != value)
                {
                    epu_lamp_status = value;
                    updated = true;

                    if (epu_lamp_status == FlightData::EPUOn)
                    {
                        std::cout << std::endl << "EPUOn: On";
                    }
                    else
                    {
                        std::cout << std::endl << "EPUOn: Off";
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

                    std::cout << std::endl << "Speed brake position: " << value;
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

                    if (any_changed)
                    {
                        target.send_message(message);
                    }
                }
            }
        }
    }

    if (quit) target.send_message("r");

    UnmapViewOfFile(flightdata);
    UnmapViewOfFile(flightdata2);
    UnmapViewOfFile(intelliVibeData);

    CloseHandle(hFalconSharedMemoryAreaMapFile);
    CloseHandle(hFalconSharedMemoryArea2MapFile);
    CloseHandle(hFalconIntellivibeSharedMemoryArea);


    target.break_connection();

    return EXIT_SUCCESS;
}
