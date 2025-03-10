// BMS2Target.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <format>

#include "target.h"
#include "FlightData.h"
#include "IVibeData.h"

const char* VERSION = "1.0.2";

const char* LED_STATE_OFF   = "0";
const char* LED_STATE_ON    = "1";
const char* LED_STATE_FLASH = "8";

int main()
{
    std::cout << "BMS2Target v" << VERSION << std::endl << std::endl;

    SOCKET s = INVALID_SOCKET;

    std::cout << "Waiting to connect..." << std::endl;

    Target target;
    while (!target.create_connection())
    {
        Sleep(1000);
    }
    std::cout << "Connected to T.A.R.G.E.T." << std::endl;

    std::string message("vBMS2Target v" + std::string(VERSION));
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

        bool flight_ended = false;

        unsigned int value;
        bool updated;

        std::string message("mF-16C_50");
        target.send_message(message);

        while (!intelliVibeData->IsExitGame)
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

                flight_ended = false;

                std::cout << std::endl << "Start flight!";
            }

            while (intelliVibeData->In3D)
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

                value = (unsigned int)(flightdata->speedBrake * 5); // convert from 0 to 1 to 0 to 5 steps of 1
                if (speed_brake_position != value)
                {
                    speed_brake_position = value;
                    updated = true;

                    std::cout << std::endl << "Speed brake position: " << value;
                }

                value = (unsigned int)(flightdata->speedBrake * 5); // convert from 0 to 1 to 0 to 5 steps of 1
                if (speed_brake_position != value)
                {
                    speed_brake_position = value;
                    updated = true;

                    std::cout << std::endl << "Speed brake position: " << value;
                }

                if (updated)
                {
                    std::string message("u");

                    (nose_gear_down_lamp_state  == FlightData::NoseGearDown)  ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (left_gear_down_lamp_state  == FlightData::LeftGearDown)  ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (right_gear_down_lamp_state == FlightData::RightGearDown) ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (gear_handle_lamp_state     == FlightData::GEARHANDLE)    ? message += LED_STATE_ON : message += LED_STATE_OFF;

                    if (aux_power_lamp_state & FlightData::AuxPwr)
                    {
                        // rwr_search_status
                        if (aux_search_lamp_state & FlightData::AuxSrch)
                        {
                            if ((aux_search_lamp_flash_state & FlightData2::AuxSrch) == FlightData2::AuxSrch)
                            {
                                message += LED_STATE_FLASH;
                            }
                            else
                            {
                                message += LED_STATE_ON;
                            }
                        }
                        else
                        {
                            message += LED_STATE_OFF;
                        }

                        // rwr_activity_status not used by target script - it should be instead of rwr_a_power_status
                        (aux_act_lamp_state == FlightData::AuxAct) ? message += LED_STATE_ON : message += LED_STATE_OFF;

                        // rwr_act_power_status - resuse aux_act_lamp_state
                        (aux_act_lamp_state == FlightData::AuxAct) ? message += LED_STATE_ON : message += LED_STATE_OFF;

                        // rwr_alt_low_status
                        (aux_low_lamp_state == FlightData::AuxLow) ? message += LED_STATE_ON : message += LED_STATE_OFF;

                        // rwr_alt_status - set to aux_power_lamp_state
                        (aux_power_lamp_state == FlightData::AuxPwr) ? message += LED_STATE_ON : message += LED_STATE_OFF;

                        // rwr_power_status
                        message += LED_STATE_ON;
                    }
                    else
                    {
                        message += "000000";
                    }

                    (jfs_run_lamp_state   == FlightData::JFSOn)   ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (main_gen_lamp_state  == FlightData::MainGen) ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (stby_gen_lamp_status == FlightData::StbyGen) ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (flcs_rly_lamp_status == FlightData::FlcsRly) ? message += LED_STATE_ON : message += LED_STATE_OFF;
                    (epu_lamp_status      == FlightData::EPUOn)   ? message += LED_STATE_ON : message += LED_STATE_OFF;

                    // Speed Brake position
                    message += speed_brake_position + '0';

                    target.send_message(message);
                }
            }
        }
    }

    UnmapViewOfFile(flightdata);
    UnmapViewOfFile(flightdata2);
    UnmapViewOfFile(intelliVibeData);

    CloseHandle(hFalconSharedMemoryAreaMapFile);
    CloseHandle(hFalconSharedMemoryArea2MapFile);
    CloseHandle(hFalconIntellivibeSharedMemoryArea);


    target.break_connection();

    return EXIT_SUCCESS;
}
