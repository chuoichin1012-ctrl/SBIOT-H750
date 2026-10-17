// Board services shared by the bootloader and the app (spec §6.1, §11.2, §11.4).
#pragma once

// Kicks both watchdogs: toggles WDI (SGM706 through Q6, which also blinks LED WORK) and reloads
// IWDG1. Without R20 the SGM706 cannot reset the MCU, so IWDG1 is the one that does.
void board_kick(void);
// The two halves of a kick, for a paced LED WORK (Plan 11a task 0): IWDG1 at every call, WDI by pace_due.
void board_iwdg_reload(void);
void board_wdi_toggle(void);

#include <stdbool.h>

// The 74HC573's LE and OE: not CubeMX's, set up here by hand (dohold.h, the user 2026-10-17).
#define LATCH_LE_Pin GPIO_PIN_14
#define LATCH_LE_GPIO_Port GPIOD
#define LATCH_OE_Pin GPIO_PIN_15
#define LATCH_OE_GPIO_Port GPIOD

// The first thing done to the latch at boot (spec §11.2 step 2). hold (Restart Hold): LE and OE driven low at
// once, never high: the latch keeps what it holds and stays enabled. Otherwise OE high, D1 = D2 = 0 latched,
// OE low: every output off. Before MX_GPIO_Init is fine (it no longer touches LE or OE).
void board_latch_init(bool hold);
