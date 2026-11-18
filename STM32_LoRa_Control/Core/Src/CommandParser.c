/*
 * CommandParser.c
 *
 *  Created on: Nov 18, 2026
 *      Author: demid
 */



#include "CommandParser.h"
#include "UART.h"
#include "SX1278.h"

#include <string.h>

static SystemMode_t current_mode = MODE_RX;

void CommandParser_Init(void)
{
    current_mode = MODE_RX;

    /* Start radio reception separately, after SX1278_Init succeeds. */
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
            "  mode tx  - Prepare to transmit\r\n"
            "  mode rx  - Start continuous reception\r\n"
            "  test     - Send TEST packet in TX mode\r\n"
        );
    }
    else if (strcmp(command, "status") == 0)
    {
        DebugUART_Print("\r\n--- System Status ---\r\n");
        DebugUART_Printf(
            "Selected role: %s\r\n",
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
        /* TX role waits in standby until a packet is ready. */
        SX1278_Status result = SX1278_SetMode(SX1278_MODE_STANDBY);

        if (result == SX1278_OK)
        {
            current_mode = MODE_TX;
            DebugUART_Print("Mode: TX, ready to send\r\n");
        }
        else
        {
            DebugUART_Printf("TX mode ERROR: %d\r\n", (int)result);
        }
    }
    else if (strcmp(command, "mode rx") == 0)
    {
        SX1278_Status result = SX1278_StartReceive();

        if (result == SX1278_OK)
        {
            current_mode = MODE_RX;
            DebugUART_Print("Mode: RX, listening\r\n");
        }
        else
        {
            DebugUART_Printf("RX mode ERROR: %d\r\n", (int)result);
        }
    }
    else if (strcmp(command, "test") == 0)
    {
        if (current_mode != MODE_TX)
        {
            DebugUART_Print("First enter: mode tx\r\n");
            return;
        }

        const uint8_t message[] = "TEST";
        uint32_t start = HAL_GetTick();

        SX1278_Status result = SX1278_Send(
            message,
            (uint8_t)(sizeof(message) - 1U),
            1000U
        );

        uint32_t elapsed = HAL_GetTick() - start;

        if (result == SX1278_OK)
        {
            /* This is the send call duration, not an ACK or exact airtime. */
            DebugUART_Printf(
                "TX done: TEST, send call took %lu ms\r\n",
                (unsigned long)elapsed
            );
        }
        else
        {
            DebugUART_Printf("TX ERROR: %d\r\n", (int)result);
        }
    }
    else
    {
        DebugUART_Printf("Unknown command: %s\r\n", command);
        DebugUART_Print("Type 'help' for commands\r\n");
    }
}

