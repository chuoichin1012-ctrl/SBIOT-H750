// OTA on the app side (spec §11.3): stream an image into staging and hand it to the bootloader,
// and confirm or roll back a new image. Used by POST /api/ota (Plan 06) and the UART7 bench
// upload (Plan 04b). Return codes map to the HTTP answers.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bootrec.h"
#include "handoff.h"
#include "nor.h"
#include "ota_image.h"
#include "sha256.h"

enum {
    OTA_OK = 0,
    OTA_EBUSY = -1,    // 409: the running image is not confirmed yet, or the record is not writable
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

// Starts an upload of `total` bytes. Refuses (OTA_EBUSY) unless the record says CONFIRMED (or there
// is none) and the boot was not degraded, so BACKUP can never overwrite the last good image; a
// record that cannot be read refuses with OTA_EFLASH.
int ota_stage_begin(ota_stage_t *st, nor_t *spi, const handoff_t *ho, uint32_t total);
// Takes the next chunk. Each 4 KB staging sector is erased just before the first byte lands in it.
// The header is checked as soon as its core has arrived (magic, CRC, hardware id, size == total).
int ota_stage_write(ota_stage_t *st, const void *data, uint32_t len);
// After the last chunk: SHA-256, vectors, a full read-back check of staging, then the PENDING
// record. Only then is the boot state changed.
int ota_stage_finish(ota_stage_t *st);

// TRIAL -> CONFIRMED (spec §11.3: all services up for 30 s). OTA_OK too when already CONFIRMED.
int ota_confirm(nor_t *spi);
// TRIAL -> RESTORE(0): a configured service failed to start.
int ota_rollback(nor_t *spi);
