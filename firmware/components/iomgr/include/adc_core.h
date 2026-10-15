// The AI logic (spec §6.6, §12.1 "ADC", Plan 07b) as pure C, host-tested (test_adc_core): the handoff of
// the BDMA's two halves, the published 100 ms window, faults (both channels at once), the conversion of
// a code to µA with the two-point calibration, and the factory record that holds it (SPI4 0x3F0000,
// spec §5.2). adc_drv.c and iomgr.c drive it on the board.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADC_CH 2
#define ADC_HALF 100  // pairs of samples in one half: 100 ms at 1 kHz

typedef struct {
    float gain[ADC_CH], offset_ma[ADC_CH];  // I' = gain × I + offset
} adc_cal_t;

typedef struct {
    adc_cal_t cal;
    bool calibrated;     // from the factory record (else gain 1, offset 0)
    float mean[ADC_CH];  // codes: the mean of the last published window
    bool published;      // a window came since the start or the last fault
    bool dead;           // the ADC never came up (init or calibration failed)
    uint32_t windows, late;
} adc_core_t;

void adc_core_init(adc_core_t *a, const adc_cal_t *cal, bool calibrated);

/** Reads the ISR's half counter (on the board, a volatile read). */
typedef uint32_t (*adc_seq_fn)(void *ctx);
/**
 * A full half (pairs of INP0, INP1). seq_start is the ISR's counter read before the sum; seq_read is
 * called after it: a different value means the BDMA may have written into this half while it was
 * summed, so the window is dropped and counted in `late`. True when the window was published.
 */
bool adc_core_half(adc_core_t *a, const uint16_t half[ADC_HALF][ADC_CH], uint32_t seq_start, adc_seq_fn seq_read, void *ctx);
/** OVR, a BDMA error: both channels fail until a full new half is published. */
void adc_core_fail(adc_core_t *a);
/** The ADC never came up: both channels fail for the whole run. */
void adc_core_dead(adc_core_t *a);

bool adc_core_ok(const adc_core_t *a, int ch);
/** The channel's current in µA, calibrated; 0 when not ok (or below 0). */
int32_t adc_core_ua(const adc_core_t *a, int ch);
/** A code (the mean) as the nominal current in mA: 3.0 V reference, 0.499 gain, 120 Ω shunt. */
float adc_nominal_ma(float code);

// ---- factory record -----------------------------------------------------------------------------

#define FREC_SIZE 64u
#define FREC_A 0x3F0000u  // SPI4: two copies, one per 4 KB sector
#define FREC_B 0x3F1000u

/** One copy: magic "SBFR", version 1, length 64, sane coefficients, CRC-32 over bytes 0..59. */
bool frec_parse(const uint8_t *b, size_t len, adc_cal_t *out);
void frec_encode(const adc_cal_t *cal, uint8_t out[FREC_SIZE]);
/** The two copies as read (NULL: the read failed): the first valid wins; none gives the nominal
 * coefficients. True when calibrated. */
bool frec_choose(const uint8_t *a, const uint8_t *b, adc_cal_t *out);
