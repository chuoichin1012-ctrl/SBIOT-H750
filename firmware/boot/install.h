// The bootloader's install logic (spec §11.2), one decision per boot:
// - Reload held for 5 s: restore the factory image into QSPI;
// - factory slot blank and QSPI valid: seed the factory slot from QSPI (once, after production);
// - a valid image in staging that QSPI does not already hold: install it into QSPI;
// - run QSPI if it passes img_check, else halt.
// No boot state record, no trial and no automatic recovery: when nothing runnable is left the
// bootloader halts and waits for the user. Plain C over nor_t, so the host tests run it on
// simulated flash with power cuts at every erase and program.
#pragma once
#include <stdbool.h>
#include "nor.h"

typedef enum {
    INSTALL_JUMP,  // the image in QSPI passed img_check: run it
    INSTALL_HALT,  // nothing runnable: blink fast, keep kicking, wait for Reload held 5 s (or SWD)
} install_result_t;

enum {                  // install_t.did: what this boot wrote (printed on UART7)
    DID_SEED = 1,       // QSPI copied into the blank factory slot
    DID_SEED_FAIL = 2,  // that copy failed (the boot goes on)
    DID_INSTALL = 4,    // staging copied into QSPI
    DID_RESTORE = 8,    // factory copied into QSPI (Reload)
};

typedef enum {
    WHY_NONE,
    WHY_NO_IMAGE,     // QSPI fails its check and staging holds no image to install
    WHY_COPY,         // copying into QSPI failed: flash error, deadline, or the copy fails its check
    WHY_FACTORY_BAD,  // Reload: the factory image fails its check (nothing was written)
    WHY_STAGING,      // Reload: the first sector of staging could not be erased
} install_why_t;

typedef struct {
    nor_t *qspi;         // the running image, offset QSPI_SLOT
    nor_t *spi;          // staging and factory slots (flashmap.h)
    void (*kick)(void);  // between chunks of long work (may be NULL)
    bool reload;         // Reload was held for 5 s (boot.c samples the button)
    unsigned did;        // result: DID_* bits
    install_why_t why;   // result: why INSTALL_HALT
} install_t;

install_result_t install_run(install_t *in);
