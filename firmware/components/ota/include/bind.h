// Anti-clone binding (spec §11.5): ties the image in QSPI to the chip's 96-bit UID. The bootloader
// writes the binding record at the first boot and checks it at every boot, before anything else;
// a record made on another chip (or with another key) erases the QSPI application slot and halts.
// Record at QSPI_BIND: 0 magic "SBBD" (programmed last), 4 HMAC-SHA256(key, "SBIOT-H750 bind" || UID),
// 36 CRC-32 of bytes 0..35 (little endian).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"

#define BIND_LEN 40u
#define BIND_UID_LEN 12u
#define BIND_KEY_LEN 32u

typedef enum {
    BIND_OK,        // the record matches this chip: go on
    BIND_WRITTEN,   // there was no record (first boot): written and read back, go on
    BIND_EREAD,     // the record cannot be read: halt, nothing written
    BIND_ECORRUPT,  // magic present, CRC wrong: halt, nothing written
    BIND_EWRITE,    // writing the record failed: halt (the next boot writes it again)
    BIND_CLONE,     // a valid record of another chip or key: the application slot was erased; halt
} bind_result_t;

// The record this chip must hold.
void bind_record(const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN], uint8_t rec[BIND_LEN]);
// The app side: rec as read from 0x90000000 + QSPI_BIND.
bool bind_matches(const uint8_t rec[BIND_LEN], const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN]);
// The bootloader side, steps 1-5 of spec §11.5. kick (may be NULL) is called between sector erases.
bind_result_t bind_check(nor_t *qspi, const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void));
