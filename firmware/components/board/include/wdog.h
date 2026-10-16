// wdog task (spec §6.1, §11.4): at priority 31, every 50 ms it reloads IWDG1 and, at the LED WORK rhythm of
// its mode, toggles WDI (Plan 11a task 0: 1 s blink running, 0.2 s busy, 0.5 s halted).
#pragma once
#include <stdbool.h>
#include <stdint.h>

void wdog_task(void *arg);
// Stops kicking for good; IWDG1 resets the board about 1.6 s later (bench test, TRIAL deadlines).
void wdog_stop(void);
// An OTA transfer is running (the web or UART7 upload): LED WORK blinks fast. Counted, so nested calls pair up.
void wdog_set_busy(bool on);
// The app halted (the binding record refused): LED WORK blinks at the halt rhythm, still kicking.
void wdog_set_halt(void);
// Largest interval between two IWDG1 reloads since boot, in microseconds (iwdgMaxGapMs, §6.1: < 400 ms).
uint32_t wdog_max_gap_us(void);
// Largest interval between two WDI toggles since boot, in microseconds (wdiMaxGapMs: < 600 ms, inside the
// SGM706's 1.0 s minimum).
uint32_t wdog_wdi_max_gap_us(void);
