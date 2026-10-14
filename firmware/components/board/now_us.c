#include "now_us.h"
#include "stm32h7xx.h"
#include "tick64.h"

static tick64_t s_tick;

uint64_t now_us(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint64_t t = tick64_update(&s_tick, TIM2->CNT);
    __set_PRIMASK(primask);
    return t;
}
