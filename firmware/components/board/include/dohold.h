// Restart Hold (the user, 2026-10-17): on a soft reset the 74HC573 is not re-latched, so the relays keep what
// it holds. LE is pulled low by R108 during the reset; OE is meant to be by R103 (not fitted on the first
// boards: it then floats on C43). This record, in one SRAM4 line that survives a soft reset, tells the next
// boot (the bootloader, then the app) what the latch holds. Pure C, host-tested (test_board).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DO_HOLD_ADDR 0x3800FBE0u  // the app's .dohold section (app.ld, gated by check_elf); read by the bootloader

typedef struct {
    uint32_t magic;
    uint8_t keep;       // keepOutputs when it was written
    uint8_t latched;    // the bits on the latch
    uint8_t commanded;  // what the IO core commands (a pulse's rest, a flip's phase, a follow's value)
    uint8_t pad;
    uint32_t crc;
} do_hold_t;

void do_hold_set(do_hold_t *r, bool keep, uint8_t latched, uint8_t commanded);
/** True with the two masks when `r` holds a record (magic and CRC right; random RAM fails). */
bool do_hold_get(const do_hold_t *r, uint8_t *latched, uint8_t *commanded);
/** Before the latch changes (Codex): a reset in the middle then finds no record and turns everything off. */
void do_hold_invalidate(do_hold_t *r);
/** Whether this boot keeps the latch: never after a power-on or a brown-out (RCC_RSR), only with a valid record
 *  written with keepOutputs on. */
bool do_hold_keep(bool por, bool bor, const do_hold_t *r);
