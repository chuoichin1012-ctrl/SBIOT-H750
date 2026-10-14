// Microseconds since TIM2 started (spec §4). Safe from tasks and ISRs. The wdog task calls it every
// 200 ms, far inside the 71-minute limit of tick64_update().
#pragma once
#include <stdint.h>

uint64_t now_us(void);
