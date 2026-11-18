/*
 * UART.c
 *
 *  Created on: Nov 18, 2026
 *      Author: demid
 */


#include "UART.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static UART_HandleTypeDef *debug_uart = NULL;

static uint8_t rx_byte;

static uint8_t rx_buffer[DEBUG_UART_RX_BUFFER_SIZE];

static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

void DebugUART_Init(UART_HandleTypeDef *huart)
{
    debug_uart = huart;

    rx_head = 0;
    rx_tail = 0;

    HAL_StatusTypeDef status =
        HAL_UART_Receive_IT(debug_uart, &rx_byte, 1);

    if (status != HAL_OK)
    {
        DebugUART_Printf("UART RX ERROR: %d\r\n", status);
    }
    else
    {
        DebugUART_Print("UART RX started OK\r\n");
    }
}

void DebugUART_Print(const char *str)
{
    if (debug_uart == NULL || str == NULL)
        return;

    HAL_UART_Transmit(
        debug_uart,
        (uint8_t *)str,
        (uint16_t)strlen(str),
        HAL_MAX_DELAY
    );
}

void DebugUART_Printf(const char *format, ...)
{
    char buffer[128];

    va_list args;
    va_start(args, format);

    int len = vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    if (len <= 0 || debug_uart == NULL)
        return;

    if (len >= (int)sizeof(buffer))
        len = sizeof(buffer) - 1;

    HAL_UART_Transmit(
        debug_uart,
        (uint8_t *)buffer,
        (uint16_t)len,
        HAL_MAX_DELAY
    );
}

uint16_t DebugUART_Available(void)
{
    uint16_t head = rx_head;
    uint16_t tail = rx_tail;

    if (head >= tail)
        return head - tail;

    return DEBUG_UART_RX_BUFFER_SIZE - tail + head;
}

int DebugUART_Read(void)
{
    if (rx_head == rx_tail)
        return -1;

    uint8_t data = rx_buffer[rx_tail];

    rx_tail = (rx_tail + 1) % DEBUG_UART_RX_BUFFER_SIZE;

    return data;
}

uint8_t DebugUART_ReadLine(char *buffer, uint16_t size)
{
    static char line[DEBUG_UART_LINE_SIZE];
    static uint16_t index = 0;
    static uint8_t discard = 0;

    if (buffer == NULL || size == 0)
        return 0;

    while (DebugUART_Available())
    {
        int data = DebugUART_Read();

        if (data < 0)
            break;

        char c = (char)data;

        if (c == '\r' || c == '\n')
        {
            if (discard)
            {
                discard = 0;
                index = 0;
                continue;
            }

            if (index == 0)
                continue;

            if (index >= size)
            {
                index = 0;
                return 0;
            }

            memcpy(buffer, line, index);
            buffer[index] = '\0';

            index = 0;
            return 1;
        }

        if (discard)
            continue;

        if (index < sizeof(line) - 1)
        {
            line[index++] = c;
        }
        else
        {
            index = 0;
            discard = 1;
        }
    }

    return 0;
}

void DebugUART_RxCallback(UART_HandleTypeDef *huart)
{
    if (huart != debug_uart)
        return;

    uint16_t next = (rx_head + 1) % DEBUG_UART_RX_BUFFER_SIZE;

    if (next != rx_tail)
    {
        rx_buffer[rx_head] = rx_byte;
        rx_head = next;
    }

    HAL_UART_Receive_IT(debug_uart, &rx_byte, 1);
}

