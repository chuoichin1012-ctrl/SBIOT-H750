// Random numbers (spec §7.3, Plan 06a). Two users, two rules:
// - board_rng: the hardware RNG only. It answers an error when the RNG fails, and is the only entropy
//   mbedTLS will be given.
// - board_rand32: LWIP_RAND (DHCP xid, DNS id and port, local ports). When the RNG fails it falls back
//   to a xorshift seeded from the UID and the clock, which is not secret but keeps the network working.
#pragma once
#include <stddef.h>
#include <stdint.h>

/** Starts the RNG (HSI48 kernel clock); 0, or -1 when it failed. Call once, from a task. */
int board_rng_init(void);
/** Fills buf from the hardware RNG; 0, or -1 when the RNG failed (restarted once first). */
int board_rng(void *buf, size_t len);
/** For LWIP_RAND only. */
uint32_t board_rand32(void);
