#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H

#include "main.h"
#include "ControlProtocol.h"

/* TIM2_CH1 / PA0, PWM mode 1, active HIGH, PSC=7 and ARR=999 at 8 MHz. */
HAL_StatusTypeDef PwmOutput_Init(TIM_HandleTypeDef *timer);
bool PwmOutput_ApplyCommand(ControlCommand command);
void PwmOutput_Stop(void);
uint8_t PwmOutput_GetDuty(void);

#endif
