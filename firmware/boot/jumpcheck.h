// The two checks before the bootloader jumps (spec §11.1, last bullets). The image header checks
// (magic, CRC, hardware id, size, SHA-256) come with mkimage.py in Plan 04; until then the reset
// vector is bounded by the whole slot.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_BASE 0x90000000u
#define APP_VECTORS 0x90000400u    // header 1 KB, then the vector table
#define APP_SLOT_SIZE 0x00170000u  // 1 472 KB, header included (spec §5.1)

// msp and reset are the first two words of the vector table at APP_VECTORS.
bool boot_app_ok(uint32_t msp, uint32_t reset);
