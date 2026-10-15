// One word from the STM32H7 RNG with its error checks (RM0433 §34.3.5-§34.3.7). The H7 HAL checks
// the seed error only on parts with RNG_CR_CONDRST, which the H750 lacks, so HAL_OK there does not
// mean the word is good (Codex 06a review). Pure C over register accessors: test_rng fakes them.
#pragma once
#include <stdint.h>

#define RNG_DRDY (1u << 0)
#define RNG_CECS (1u << 1)  // clock error, now
#define RNG_SECS (1u << 2)  // seed error, now
#define RNG_SEIS (1u << 6)  // seed error seen (cleared by writing 0)

typedef struct {
    uint32_t (*sr)(void *ctx);
    uint32_t (*dr)(void *ctx);
    void (*clear_seis)(void *ctx);
    void *ctx;
    unsigned spins;  // DRDY polls before a word counts as missing
} rng_io_t;

/**
 * A word into *w and 0, or -1 with *w untouched. A seed error is recovered once as RM0433 §34.3.7
 * says (clear SEIS, read and drop 12 words without waiting for DRDY, SEIS must stay clear); a word read while, or just before,
 * an error shows is dropped. A clock error fails the word.
 */
int rng_word(const rng_io_t *io, uint32_t *w);
