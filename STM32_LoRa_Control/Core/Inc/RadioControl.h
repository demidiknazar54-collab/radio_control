#ifndef RADIO_CONTROL_H
#define RADIO_CONTROL_H

#include "SX1278.h"
#include "ControlProtocol.h"

typedef enum { MODE_RX = 0, MODE_TX = 1 } SystemMode_t;

/* Call once after GPIO setup and successful SX1278_Init. */
SX1278_Status RadioControl_Init(void);
void RadioControl_Process(void);
SystemMode_t RadioControl_GetMode(void);
void RadioControl_SetMode(SystemMode_t mode);
void RadioControl_SetSession(uint32_t session);
void RadioControl_SetAck(bool enabled);
void RadioControl_Send(ControlCommand command);
void RadioControl_StartTest(uint32_t count);
void RadioControl_StopTest(void);
void RadioControl_PrintStats(void);
void RadioControl_Report(uint32_t total);

#endif
