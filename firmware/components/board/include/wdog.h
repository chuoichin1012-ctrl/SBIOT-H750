// wdog task (spec §6.1, §11.4): kicks both watchdogs every 200 ms at priority 31.
#pragma once
#include <stdint.h>

void wdog_task(void *arg);
// Stops kicking for good; IWDG1 resets the board about 1.6 s later (bench test, TRIAL deadlines).
void wdog_stop(void);
// Largest interval between two kicks since boot, in microseconds (bench criterion wdiMaxGapMs, §6.1).
uint32_t wdog_max_gap_us(void);
