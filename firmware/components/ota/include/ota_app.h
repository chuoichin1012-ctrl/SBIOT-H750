// OTA on the app side (spec §11.3): stream an image into staging. The bootloader installs a valid
// staging image at the next boot; there is no record to write and nothing to confirm. Used by
// POST /api/ota (Plan 06) and the UART7 bench upload (Plan 04b). Return codes map to the HTTP answers.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"
#include "ota_image.h"
#include "sha256.h"

enum {
    OTA_OK = 0,
    OTA_ETOOBIG = -2,  // 413: larger than the slot
    OTA_EIMAGE = -3,   // 400: not an image for this board, or its SHA-256 does not match
    OTA_ESHORT = -4,   // 400: the body ended before the size the header announced
    OTA_EFLASH = -5,   // 500: staging could not be erased, programmed or read back
};

typedef struct {
    nor_t *spi;
    uint32_t total;     // image bytes announced by the caller (header + body)
    uint32_t got;       // bytes received so far
    uint32_t erased;    // staging bytes erased so far (always a 4 KB multiple)
    uint8_t page[NOR_PAGE];
    uint32_t fill;      // bytes in page, not yet programmed
    img_hdr_t hdr;      // valid once got >= IMG_CORE
    sha256_t sha;       // over the body
    int error;          // sticky first error
} ota_stage_t;

// Starts an upload of `total` bytes (header + body).
int ota_stage_begin(ota_stage_t *st, nor_t *spi, uint32_t total);
// begin erases staging's first sector, so the image staged before is gone at once.
// Takes the next chunk. Each 4 KB staging sector is erased just before the first byte lands in it;
// the magic is left blank until finish.
// The header is checked as soon as its core has arrived (magic, CRC, hardware id, size == total).
int ota_stage_write(ota_stage_t *st, const void *data, uint32_t len);
// After the last chunk: SHA-256, then the magic, then a full read-back check of staging (img_check,
// as the bootloader will do it). OTA_OK: reboot, and the bootloader installs the image.
int ota_stage_finish(ota_stage_t *st);
