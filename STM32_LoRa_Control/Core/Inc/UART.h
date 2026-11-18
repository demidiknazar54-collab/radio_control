/*
 * UART.h
 *
 *  Created on: Nov 18, 2026
 *      Author: demid
 */

#ifndef INC_UART_H_
#define INC_UART_H_

#include "main.h"
#include <stdint.h>

#define DEBUG_UART_RX_BUFFER_SIZE 256
#define DEBUG_UART_LINE_SIZE      128

void DebugUART_Init(UART_HandleTypeDef *huart);

void DebugUART_Print(const char *str);
void DebugUART_Printf(const char *format, ...);

uint16_t DebugUART_Available(void);
int DebugUART_Read(void);
uint8_t DebugUART_ReadLine(char *buffer, uint16_t size);

void DebugUART_RxCallback(UART_HandleTypeDef *huart);

#endif /* INC_UART_H_ */
