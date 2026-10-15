// Anti-clone binding (spec §11.5): ties the image in QSPI to the chip's 96-bit UID. The record is
// written at programming time by tools/provision.py (the private key stays on the production PC);
// the bootloader and the app only check it, with the public key they were built with. The
// bootloader never writes a record. A valid record of another chip or key erases the QSPI
// application slot.
// Record at QSPI_BIND (little endian): 0 magic "SBBD", 4 UID (12), 16 Ed25519 signature (64) over
// "SBIOT-H750 bind" || UID, 80 CRC-32 of bytes 0..79.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"

#define BIND_LEN 84u
#define BIND_UID_LEN 12u
#define BIND_PUB_LEN 32u

typedef enum {
    BIND_OK,        // the record belongs to this chip: go on
    BIND_EREAD,     // the record cannot be read: halt, nothing written
    BIND_ENONE,     // no record (never provisioned): halt, nothing written
    BIND_ECORRUPT,  // magic present, CRC wrong: halt, nothing written
    BIND_CLONE,     // a valid record of another chip or key: the application slot was erased; halt
} bind_result_t;

// The record belongs to this chip and key. The app calls it on the record read through XIP.
bool bind_valid(const uint8_t rec[BIND_LEN], const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN]);
// The bootloader, before anything else (spec §11.5). kick (may be NULL) is called between sector erases.
bind_result_t bind_check(nor_t *qspi, const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void));
