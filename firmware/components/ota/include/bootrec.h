// Boot state record (spec §11.2): two copies, A and B, each in its own 4 KB sector of SPI4. A write
// goes to the sector that does not hold the newest valid record: erase, body (page 0), then the
// CRC (page 1) last, so a cut write is never valid and the older record stays usable. The newest
// is the valid one with the larger seq in wrap-around order, (int32_t)(a - b) > 0.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"
#include "ota_image.h"

enum {
    BR_CONFIRMED = 1,
    BR_PENDING = 2,  // written by the app when staging holds a complete image
    BR_BACKUP = 3,   // copying QSPI -> backup, checkpoint `offset`
    BR_WRITE = 4,    // copying staging -> QSPI, checkpoint `offset`
    BR_VERIFY = 5,   // QSPI against the staging header
    BR_TRIAL = 6,    // the new image runs for the `trial`-th time and must confirm
    BR_RESTORE = 7,  // copying backup -> QSPI, checkpoint `offset`
};

// Why the last transition happened (logged by the app; spec §11.2 "lý do").
enum {
    RSN_NONE = 0,
    RSN_FIRST_BOOT,       // no valid record: created from what the flash holds
    RSN_STAGING_BAD,      // PENDING, but staging failed its check
    RSN_BACKUP_VERIFY,    // backup copy failed its SHA twice: install cancelled
    RSN_BACKUP_ABORTED,   // BACKUP attempts > 3: install cancelled
    RSN_QSPI_BAD,         // the running image failed its check
    RSN_BACKUP_BAD,       // RESTORE found no valid backup
    RSN_TRIAL_EXCEEDED,   // the new image never confirmed in 3 boots
    RSN_TRIAL_IMAGE_BAD,  // the new image failed its check before a trial boot
    RSN_APP_ROLLBACK,     // the app asked for RESTORE (a configured service failed to start)
    RSN_WRITE_EXHAUSTED,  // WRITE attempts or verifications used up
    RSN_INSTALLED,        // VERIFY passed
    RSN_RESTORED,         // RESTORE finished and verified
    RSN_CONFIRMED,        // the app confirmed the new image
    RSN_RESCUE_RETRY,     // rescue retried a recovery
};

typedef struct {
    uint32_t seq;
    uint8_t state;
    uint8_t offset;       // checkpoints (64 KB) done in BACKUP/WRITE/RESTORE
    uint8_t trial;        // trial boots started
    uint8_t attempts;     // starts of checkpoint `offset` not finished
    uint8_t verify_fail;  // failed VERIFY / post-RESTORE checks in this phase
    uint8_t degraded;     // the previous boot could not write the record
    uint8_t reason;       // RSN_*
    uint8_t staging[IMG_CORE];  // header core of the image being installed
    uint8_t backup[IMG_CORE];   // header core of the image in (or going to) backup
} bootrec_t;

// Reads both copies: BOOTREC_VALID with the newest valid one in r, BOOTREC_NONE when neither copy
// is valid, or a NOR error when a copy could not be read (then nothing is known about the state).
enum { BOOTREC_VALID = 1, BOOTREC_NONE = 0 };
int bootrec_read(nor_t *spi, bootrec_t *r);
// Writes r as the next record (r->seq is set to newest + 1). Returns NOR_OK, a NOR error, or
// NOR_EIO when the read-back does not match. Never writes when the newest copy cannot be
// determined (a read error), so a valid newest record is never erased. A failure before the CRC
// page is programmed leaves the previous record the newest; when only the read-back fails, the
// new record may have reached the flash intact and be the newest on the next read.
int bootrec_write(nor_t *spi, bootrec_t *r);
// For the tests: encodes r into the 512 bytes a slot holds (body page and CRC page).
void bootrec_encode(const bootrec_t *r, uint8_t out[512]);
