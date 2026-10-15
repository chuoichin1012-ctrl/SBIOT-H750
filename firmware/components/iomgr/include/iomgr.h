// The gateway's IO on the board (spec §6.1 `io` and `reload`, §6.6, §11.4, Plan 07b): DI1/DI2 (PB7/PB6,
// active low), DO1/DO2 through the 74HC573 (PD10/PD11, LE PD14), AI1/AI2 from ADC3, and the Reload
// button (PA8). io_core, adc_core and reload hold the logic; this file owns the pins, the mutex and the
// tasks. Settings come from `io` and `system` once at boot (spec §9.1).
//  - io (prio 6): the ADC halves and faults, DI, the DO deadlines and the latch, the timers, the button;
//    bounded work per wake-up and never a flash write.
//  - reload (prio 3): the slow work: the factory reset, and the one writer of keepOutputs.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "nor.h"

/** Restores keepOutputs, reads the factory record from `spi4`, starts the tasks and the ADC. */
void iomgr_start(const cJSON *io, const cJSON *sys, nor_t *spi4);

/** DO i (0-based) as a command from the web (later MQTT, linkage). 0, or -1 for no such output. */
int iomgr_do_set(int i, bool on);
bool iomgr_do_get(int i);
bool iomgr_di_get(int i);
/** AI i in µA (0 when not ok), and whether it is a measurement. */
int32_t iomgr_ai_ua(int i);
bool iomgr_ai_ok(int i);
/** {"do":[..],"di":[..],"ai":[µA..],"ao":[]} for GET /api/io (caller deletes). */
cJSON *iomgr_state_json(void);

typedef struct {
    bool ai_calibrated;
    uint32_t adc_irqs, adc_late, adc_errors, adc_recoveries, adc_windows;
    float mean[2];                    // the raw window means (codes), for the bench calibration
    uint32_t cr, cfgr, cfgr2, smpr1;  // ADC3 registers
} io_status_t;
void iomgr_status(io_status_t *s);
