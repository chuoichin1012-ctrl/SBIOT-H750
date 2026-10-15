// ADC3 for AI1/AI2 (spec §6.6, Plan 07b): INP0 and INP1, 16 bit, oversampling ×16 from one TIM6 trigger
// at 1 kHz, BDMA circular into two halves of 100 pairs (SRAM4 0x3800FC00, non-cacheable). One interrupt
// per half (HT, TC) and one per fault (BDMA TE, ADC OVR): they notify the io task, which sums the half
// (adc_core) or runs the recovery. Nothing happens per sample.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "adc_core.h"
#include "task.h"

#define ADC_N_HALF 1u   // notification bits to the io task
#define ADC_N_FAULT 2u

/** Sets up and calibrates ADC3, then starts TIM6, the BDMA and the ADC. 0, or -1 (logged): the AI stay
 * failed for the run. `io` gets the notifications. */
int adc_drv_start(TaskHandle_t io);
/** The half the ISR handed over last, and the ISR's half counter (incremented at each HT and TC). */
const uint16_t (*adc_drv_half(void))[ADC_CH];
uint32_t adc_drv_seq(void);
/** The recovery of spec §6.6: TIM6 off, ADSTP, BDMA off, flags cleared, BDMA reloaded and on, ADSTART,
 * TIM6 on last. The calibration stays (no deep power-down). */
void adc_drv_recover(void);
/** Bench: the BDMA channel off, so the next conversion overruns. */
void adc_drv_inject_fault(void);

typedef struct {
    uint32_t irqs, errors, recoveries;
    uint32_t cr, cfgr, cfgr2, smpr1;  // ADC3 registers, for the bench (BOOST, OVSR, OVSS, SMP)
} adc_drv_info_t;
void adc_drv_info(adc_drv_info_t *i);
