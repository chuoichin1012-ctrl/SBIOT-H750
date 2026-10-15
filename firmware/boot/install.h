// The bootloader's install and recovery logic (spec §11.2): one decision per boot from the boot
// state record, the three image sources and the handoff block. Plain C over nor_t, so the host
// tests run it on simulated flash with power cuts at every erase and program.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bootrec.h"
#include "handoff.h"
#include "nor.h"
#include "ota_image.h"

typedef enum {
    INSTALL_JUMP,          // run the image in QSPI; it passed img_check (degraded: no writable record)
    INSTALL_REBOOT,        // reset now and boot again (row 5; a checkpoint that failed its read-back)
    INSTALL_WATCHDOG,      // stop kicking and let IWDG1 reset the board (row 2: a copy missed its deadline)
    INSTALL_RESCUE_RETRY,  // blink fast, keep kicking, call install_rescue_retry() after 60 s
    INSTALL_RESCUE_HALT,   // blink fast, keep kicking, wait for SWD (row 1: no valid source left)
} install_result_t;

#define INSTALL_ROW(n) (1u << (n))  // decision-table rows in install_t.rows
#define INSTALL_BUG (1u << 31)      // the decision loop did not settle (never expected)

typedef struct {
    nor_t *qspi;            // the running image, offset QSPI_SLOT
    nor_t *spi;             // records, staging, backup (flashmap.h)
    handoff_t *ho;          // checked with handoff_load() by the caller
    void (*kick)(void);     // between chunks of long work (may be NULL)
    // results
    bool degraded;          // INSTALL_JUMP without a writable boot record
    uint32_t rows;          // INSTALL_ROW(n) for every decision-table row this call went through
    bootrec_t rec;          // the record as last read or written
    // private
    bool prev_degraded;
    signed char vq, vb, vs;  // validity cache: -1 unknown, else 0/1
    img_hdr_t hq, hb, hs;
} install_t;

install_result_t install_run(install_t *in);
// After INSTALL_RESCUE_RETRY and the 60 s pause: resets the counters and retries the recovery,
// from backup first, else from staging (spec §11.2, "Cứu hộ có thử lại").
install_result_t install_rescue_retry(install_t *in);
