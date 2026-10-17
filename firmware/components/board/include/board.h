// Board services shared by the bootloader and the app (spec §6.1, §11.2, §11.4).
#pragma once

// Kicks both watchdogs: toggles WDI (SGM706 through Q6, which also blinks LED WORK) and reloads
// IWDG1. Without R20 the SGM706 cannot reset the MCU, so IWDG1 is the one that does.
void board_kick(void);
// The two halves of a kick, for a paced LED WORK (Plan 11a task 0): IWDG1 at every call, WDI by pace_due.
void board_iwdg_reload(void);
void board_wdi_toggle(void);

// Latches D1 = D2 = 0 into the 74HC573 and enables its outputs (spec §11.2 step 2).
// MX_GPIO_Init must have run (it leaves OE high, D1/D2/LE low).
void board_latch_zero(void);
