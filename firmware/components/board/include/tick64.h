// 64-bit microsecond clock from a free-running 32-bit counter (spec §4: now_us()).
// Hardware-free so it runs in the host tests; components/board/now_us.c feeds it TIM2.
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t last;  // raw counter value at the previous update
    uint32_t high;  // number of counter wraps seen
} tick64_t;

// Extends raw to 64 bits. Correct as long as it is called at least once per counter period
// (2^32 us = 71.6 min at 1 MHz) and never concurrently with itself.
uint64_t tick64_update(tick64_t *t, uint32_t raw);
