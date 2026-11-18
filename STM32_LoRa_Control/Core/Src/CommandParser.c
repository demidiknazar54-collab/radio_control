/*
 * CommandParser.c
 *
 *  Created on: Nov 18, 2026
 *      Author: demid
 */



#include "CommandParser.h"
#include "UART.h"

#include <string.h>
#include <stdio.h>

static SystemMode_t current_mode = MODE_RX;

void CommandParser_Init(void)
{
    current_mode = MODE_RX;

    DebugUART_Print(
        "\r\nCommand Parser initialized\r\n"
        "Type 'help' for commands\r\n"
    );
}

SystemMode_t CommandParser_GetMode(void)
{
    return current_mode;
}

void CommandParser_Process(const char *command)
{
    if (command == NULL)
        return;

    if (strcmp(command, "help") == 0)
    {
        DebugUART_Print(
            "\r\nAvailable commands:\r\n"
            "  help     - Show commands\r\n"
            "  status   - System status\r\n"
            "  mode tx  - Switch to TX mode\r\n"
            "  mode rx  - Switch to RX mode\r\n"
        );
    }
    else if (strcmp(command, "status") == 0)
    {
        DebugUART_Print("\r\n--- System Status ---\r\n");

        DebugUART_Printf(
            "Mode: %s\r\n",
            current_mode == MODE_TX ? "TX" : "RX"
        );

        DebugUART_Printf(
            "Uptime: %lu ms\r\n",
            (unsigned long)HAL_GetTick()
        );

        DebugUART_Print("---------------------\r\n");
    }
    else if (strcmp(command, "mode tx") == 0)
    {
        current_mode = MODE_TX;

        DebugUART_Print("Mode changed to TX\r\n");
    }
    else if (strcmp(command, "mode rx") == 0)
    {
        current_mode = MODE_RX;

        DebugUART_Print("Mode changed to RX\r\n");
    }
    else
    {
        DebugUART_Printf(
            "Unknown command: %s\r\n",
            command
        );

        DebugUART_Print("Type 'help' for commands\r\n");
    }
}
