// The app's HAL timebase is TIM2 through now_us(): SysTick belongs to FreeRTOS, and no interrupt
// is needed, so HAL timeouts also work before the scheduler starts and inside critical sections.
#include "now_us.h"
#include "stm32h7xx_hal.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    (void)TickPriority;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    return (uint32_t)(now_us() / 1000u);
}
