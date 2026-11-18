/*
 * CommandParser.h
 *
 *  Created on: Nov 18, 2026
 *      Author: demid
 */

#ifndef INC_COMMANDPARSER_H_
#define INC_COMMANDPARSER_H_

#include "main.h"
#include <stdint.h>

typedef enum
{
    MODE_RX = 0,
    MODE_TX = 1
} SystemMode_t;

void CommandParser_Init(void);
void CommandParser_Process(const char *command);

SystemMode_t CommandParser_GetMode(void);

#endif
