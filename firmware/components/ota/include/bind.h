// Anti-clone binding (spec §11.5): ties the image in QSPI to the chip's 96-bit UID. The record is
// written at programming time by tools/provision.py (the private key stays on the production PC);
// the bootloader and the app only check it, with the public key they were built with. The
// bootloader never writes a record. A valid record of another chip or key erases the QSPI
// application slot.
// Record version 2 at QSPI_BIND (Plan 11a, little endian): 0 magic "SBB2", 4 UID (12), 16 MAC (6), 22 SN
// (12 ASCII, SB + YYMM + 6 digits), 34 reserved (2), 36 Ed25519 signature (64) over "SBIOT-H750 bind2" ||
// bytes 4..33, 100 CRC-32 of bytes 0..99. The record's UID must be the chip's. A version 1 record ("SBBD",
// no MAC or SN) halts: the board is programmed again (the user, 2026-10-17).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"

#define BIND_LEN 104u
#define BIND_UID_LEN 12u
#define BIND_PUB_LEN 32u

typedef enum {
    BIND_OK,        // the record belongs to this chip: go on
    BIND_EREAD,     // the record cannot be read: halt, nothing written
    BIND_ENONE,     // no record (never provisioned): halt, nothing written
    BIND_ECORRUPT,  // magic present, CRC wrong: halt, nothing written
    BIND_CLONE,     // a valid record of another chip or key: the application slot was erased; halt
    BIND_EOLD,      // a version 1 record (no MAC or SN): halt, nothing written; program it again (Plan 11a)
} bind_result_t;

// The record belongs to this chip and key. The app calls it on the record read through XIP.
bool bind_valid(const uint8_t rec[BIND_LEN], const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN]);
// The MAC and the SN (NUL-terminated) the record carries; false when the SN is not SB + 10 digits. Call it on a
// record bind_valid accepted.
bool bind_identity(const uint8_t rec[BIND_LEN], uint8_t mac[6], char sn[13]);
// The bootloader, before anything else (spec §11.5). kick (may be NULL) is called between sector erases.
bind_result_t bind_check(nor_t *qspi, const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void));
