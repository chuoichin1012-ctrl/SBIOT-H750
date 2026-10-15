// Handoff block between bootloader and app (spec §11.2): 64 bytes at the end of backup SRAM, kept
// across resets while the board has power, lost on a power cycle (VBAT is tied to 3V3). A wrong
// magic or CRC (the first power-on) means all zero.
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint8_t rec_fail;   // consecutive boots that could not write the boot record
    uint8_t ram_fail;   // RAM-tracked recovery attempts (decision table row 6)
    uint8_t degraded;   // this boot runs without a writable record: the app refuses OTA (409)
    uint8_t reason;     // RSN_* of this boot's decision
    uint8_t read_fail;  // consecutive boots that reset because an image could not be read
    uint8_t pad[51];
    uint32_t crc;       // CRC-32 of the 60 bytes before it
} handoff_t;

_Static_assert(sizeof(handoff_t) == 64, "handoff block is 64 bytes");

void handoff_load(handoff_t *h);   // checks magic and CRC; resets to zero when wrong
void handoff_store(handoff_t *h);  // sets magic and CRC
