#include "PwmOutput.h"

static TIM_HandleTypeDef *pwm_timer;
static uint8_t duty_percent;

HAL_StatusTypeDef PwmOutput_Init(TIM_HandleTypeDef *timer)
{
    if (timer == NULL || timer->Instance != TIM2 ||
        timer->Init.Prescaler != 7U || timer->Init.Period != 999U)
        return HAL_ERROR;

    __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_1, 0U);
    HAL_StatusTypeDef result = HAL_TIM_PWM_Start(timer, TIM_CHANNEL_1);
    if (result == HAL_OK) {
        pwm_timer = timer;
        duty_percent = 0;
    }
    return result;
}

static void set_duty(uint8_t percent)
{
    if (pwm_timer == NULL) return;
    /* CCR=1000 with ARR=999 gives continuous HIGH in up-counting PWM mode 1. */
    uint32_t compare = ((__HAL_TIM_GET_AUTORELOAD(pwm_timer) + 1U) * percent) / 100U;
    __HAL_TIM_SET_COMPARE(pwm_timer, TIM_CHANNEL_1, compare);
    duty_percent = percent;
}

bool PwmOutput_ApplyCommand(ControlCommand command)
{
    if (pwm_timer == NULL) return false;
    switch (command) {
        case CONTROL_LEFT:  set_duty(25); break;
        case CONTROL_STOP:  set_duty(0); break;
        case CONTROL_START: set_duty(75); break;
        case CONTROL_RIGHT: set_duty(100); break;
        default: return false; /* TEST does not change the output. */
    }
    return true;
}

void PwmOutput_Stop(void) { set_duty(0); }
uint8_t PwmOutput_GetDuty(void) { return duty_percent; }
