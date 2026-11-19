#ifndef INC_COMMANDPARSER_H_
#define INC_COMMANDPARSER_H_

#include "RadioControl.h"

void CommandParser_Init(void);
void CommandParser_Process(const char *command);
SystemMode_t CommandParser_GetMode(void);

#endif
