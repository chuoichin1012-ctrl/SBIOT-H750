#include "install.h"
#include <string.h>
#include "flashmap.h"

#define CONTINUE (-1)  // a step changed the record and the decision loop goes round again
#define COPY_MISMATCH (-100)

static uint32_t image_len(const uint8_t core[IMG_CORE])
{
    uint32_t size = (uint32_t)core[28] | (uint32_t)core[29] << 8 | (uint32_t)core[30] << 16 | (uint32_t)core[31] << 24;
    return IMG_HDR_SIZE + size;
}

static uint32_t checkpoints(uint32_t len)
{
    return (len + CHECKPOINT - 1) / CHECKPOINT;
}

// ---- the three sources, checked on demand and cached until written --------------------------

// A read error is not proof of a bad image: it is not cached, and in->read_error makes the boot
// decide again after a reset (unsure()) before acting on "not valid".
static bool check(install_t *in, signed char *v, img_hdr_t *h, nor_t *n, uint32_t off)
{
    if (*v >= 0)
        return *v;
    int r = img_check(n, off, in->kick, h);
    if (r == IMG_EREAD) {
        in->read_error = true;
        return false;
    }
    *v = r == IMG_OK;
    return *v;
}

static bool vq(install_t *in) { return check(in, &in->vq, &in->hq, in->qspi, QSPI_SLOT); }
static bool vb(install_t *in) { return check(in, &in->vb, &in->hb, in->spi, SPI_BACKUP); }
static bool vs(install_t *in) { return check(in, &in->vs, &in->hs, in->spi, SPI_STAGING); }

// ---- the record --------------------------------------------------------------------------

static void set_state(install_t *in, uint8_t state, uint8_t reason)
{
    in->rec.state = state;
    in->rec.offset = 0;
    in->rec.attempts = 0;
    in->rec.reason = reason;
}

static void enter_restore(install_t *in, uint8_t reason)
{
    set_state(in, BR_RESTORE, reason);
    in->rec.verify_fail = 0;
    if (vb(in))
        memcpy(in->rec.backup, in->hb.raw, IMG_CORE);
}

static bool put(install_t *in)
{
    in->rec.degraded = in->prev_degraded;
    if (bootrec_write(in->spi, &in->rec) != NOR_OK)
        return false;
    if (in->ho->rec_fail) {
        in->ho->rec_fail = 0;
        handoff_store(in->ho);
    }
    return true;
}

// True when a decision is about to rest on an image that could not be read: reset and decide
// again, at most 3 times in a row (then an unreadable image counts as not valid).
static bool unsure(install_t *in)
{
    if (!in->read_error || in->ho->read_fail >= 3)
        return false;
    in->ho->read_fail++;
    handoff_store(in->ho);
    return true;
}

// Row 1, or a rescue retry when an image could not even be read (it may come back).
static install_result_t nothing_valid(install_t *in)
{
    if (unsure(in))
        return INSTALL_REBOOT;
    if (in->read_error)
        return INSTALL_RESCUE_RETRY;
    in->rows |= INSTALL_ROW(1);
    return INSTALL_RESCUE_HALT;
}

static install_result_t jump(install_t *in, bool degraded)
{
    in->ho->read_fail = 0;
    in->degraded = degraded;
    in->ho->degraded = degraded;
    in->ho->reason = in->rec.reason;
    handoff_store(in->ho);
    return INSTALL_JUMP;
}

// ---- copying -------------------------------------------------------------------------------

// Checkpoint k of an image of len bytes: erase the 4 KB sectors it covers (the last one rounded up,
// spec §5.2), program page by page, read back and compare with the source.
static int copy_checkpoint(install_t *in, nor_t *src, uint32_t so, nor_t *dst, uint32_t dof, uint32_t len, uint32_t k)
{
    uint32_t start = k * CHECKPOINT, end = start + CHECKPOINT < len ? start + CHECKPOINT : len;
    for (uint32_t s = start; s < end; s += NOR_SECTOR) {
        int e = dst->erase_sector(dst, dof + s);
        if (e)
            return e;
        if (in->kick)
            in->kick();
    }
    uint8_t buf[NOR_PAGE], back[NOR_PAGE];
    for (uint32_t p = start; p < end; p += NOR_PAGE) {
        uint32_t n = end - p < NOR_PAGE ? end - p : NOR_PAGE;
        int e = src->read(src, so + p, buf, n);
        if (e)
            return e;  // a read timeout stays a timeout (row 2)
        e = dst->program(dst, dof + p, buf, n);
        if (e)
            return e;
        e = dst->read(dst, dof + p, back, n);
        if (e)
            return e;
        if (memcmp(buf, back, n))
            return COPY_MISMATCH;
    }
    return NOR_OK;
}

static install_result_t rec_failed(install_t *in);

// Runs the remaining checkpoints of the current BACKUP/WRITE/RESTORE. Each start is counted in the
// record before it begins, so a reset in the middle is counted too (spec §11.2).
static int run_copy(install_t *in, nor_t *src, uint32_t so, nor_t *dst, uint32_t dof, uint32_t len, signed char *dst_valid)
{
    while (in->rec.offset < checkpoints(len)) {
        in->rec.attempts++;
        if (!put(in))
            return rec_failed(in);
        int e = copy_checkpoint(in, src, so, dst, dof, len, in->rec.offset);
        *dst_valid = -1;
        if (e == NOR_TIMEOUT) {
            in->rows |= INSTALL_ROW(2);
            return INSTALL_WATCHDOG;
        }
        if (e)
            return INSTALL_REBOOT;  // counted in attempts; rows 7-9 stop the loop
        in->rec.offset++;
        in->rec.attempts = 0;
        if (!put(in))
            return rec_failed(in);
    }
    return CONTINUE;
}

// ---- rows 3-6 and the RAM-tracked recovery ----------------------------------------------------

static install_result_t ram_path(install_t *in)
{
    in->rows |= INSTALL_ROW(6);
    if (in->ho->ram_fail < 255)
        in->ho->ram_fail++;  // counted before anything else
    handoff_store(in->ho);
    if (in->ho->ram_fail > 3)
        return INSTALL_RESCUE_RETRY;
    const uint32_t sources[2] = {SPI_BACKUP, SPI_STAGING};
    for (int i = 0; i < 2; i++) {
        img_hdr_t h;
        int c = img_check(in->spi, sources[i], in->kick, &h);
        if (c == IMG_EREAD)
            in->read_error = true;  // unreadable, not proven bad: nothing_valid() decides again
        if (c != IMG_OK)
            continue;  // a bad source never gets copied; try the next one
        uint32_t len = IMG_HDR_SIZE + h.size;
        for (uint32_t k = 0; k < checkpoints(len); k++) {
            int e = copy_checkpoint(in, in->spi, sources[i], in->qspi, QSPI_SLOT, len, k);
            in->vq = -1;
            if (e == NOR_TIMEOUT) {
                in->rows |= INSTALL_ROW(2);
                return INSTALL_WATCHDOG;
            }
            if (e)
                break;
        }
        if (vq(in)) {
            in->ho->ram_fail = 0;
            handoff_store(in->ho);
            return jump(in, true);
        }
        return INSTALL_REBOOT;  // never jump into an image that failed its check
    }
    return nothing_valid(in);
}

static install_result_t rec_failed(install_t *in)
{
    if (!vq(in) && !vb(in) && !vs(in))
        return nothing_valid(in);  // row 1 comes before every recovery branch
    in->rows |= INSTALL_ROW(3);
    if (in->ho->rec_fail < 255)
        in->ho->rec_fail++;
    handoff_store(in->ho);
    if (vq(in)) {
        in->rows |= INSTALL_ROW(4);
        return jump(in, true);  // QSPI is never erased here; a pending install waits
    }
    if (in->ho->rec_fail < 3 || unsure(in)) {  // never overwrite a QSPI image that could not be read
        in->rows |= INSTALL_ROW(5);
        return INSTALL_REBOOT;
    }
    return ram_path(in);
}

// ---- row 10: one step of the state machine ----------------------------------------------------

static int step(install_t *in)
{
    bootrec_t *r = &in->rec;
    int res;
    switch (r->state) {
    case BR_CONFIRMED:
        if (vq(in))
            return jump(in, false);
        if (vb(in)) {
            enter_restore(in, RSN_QSPI_BAD);
            break;
        }
        in->rows |= INSTALL_ROW(9);  // only staging is left (row 1 covered none)
        return INSTALL_RESCUE_RETRY;
    case BR_PENDING:
        r->verify_fail = 0;
        if (!vs(in)) {
            set_state(in, BR_CONFIRMED, RSN_STAGING_BAD);  // checked before anything is erased
            break;
        }
        memcpy(r->staging, in->hs.raw, IMG_CORE);
        if (vq(in)) {
            memcpy(r->backup, in->hq.raw, IMG_CORE);
            set_state(in, BR_BACKUP, RSN_NONE);
        } else {
            set_state(in, BR_WRITE, RSN_NONE);  // the running image is bad: keep the old backup
        }
        break;
    case BR_BACKUP:
        res = run_copy(in, in->qspi, QSPI_SLOT, in->spi, SPI_BACKUP, image_len(r->backup), &in->vb);
        if (res != CONTINUE)
            return res;
        if (vb(in) && !memcmp(in->hb.raw, r->backup, IMG_CORE)) {
            set_state(in, BR_WRITE, RSN_NONE);
            r->verify_fail = 0;
        } else if (++r->verify_fail >= 2) {
            set_state(in, BR_CONFIRMED, RSN_BACKUP_VERIFY);  // the running image is untouched
            r->verify_fail = 0;
        } else {
            set_state(in, BR_BACKUP, RSN_NONE);
        }
        break;
    case BR_WRITE:
        res = run_copy(in, in->spi, SPI_STAGING, in->qspi, QSPI_SLOT, image_len(r->staging), &in->vq);
        if (res != CONTINUE)
            return res;
        set_state(in, BR_VERIFY, RSN_NONE);
        break;
    case BR_VERIFY:
        if (vq(in) && !memcmp(in->hq.raw, r->staging, IMG_CORE)) {
            set_state(in, BR_TRIAL, RSN_INSTALLED);
            r->trial = 0;
            r->verify_fail = 0;
        } else {
            r->verify_fail++;
            set_state(in, BR_WRITE, RSN_NONE);
        }
        break;
    case BR_TRIAL:
        if (r->trial + 1 > 3) {
            enter_restore(in, RSN_TRIAL_EXCEEDED);
            break;
        }
        if (!vq(in)) {
            enter_restore(in, RSN_TRIAL_IMAGE_BAD);  // never jump into a bad image, even in TRIAL
            break;
        }
        r->trial++;
        if (!put(in))
            return rec_failed(in);  // row 4: runs degraded, cannot confirm; the trial counts only if
                                    // the record reached the flash (only its read-back failed)
        return jump(in, false);
    case BR_RESTORE:
        if (r->offset == 0) {
            if (!vb(in)) {  // checked before QSPI is erased
                if (vq(in)) {
                    set_state(in, BR_CONFIRMED, RSN_BACKUP_BAD);
                    break;
                }
                in->rows |= INSTALL_ROW(9);
                return INSTALL_RESCUE_RETRY;
            }
            memcpy(r->backup, in->hb.raw, IMG_CORE);
        }
        res = run_copy(in, in->spi, SPI_BACKUP, in->qspi, QSPI_SLOT, image_len(r->backup), &in->vq);
        if (res != CONTINUE)
            return res;
        if (vq(in) && !memcmp(in->hq.raw, r->backup, IMG_CORE)) {
            set_state(in, BR_CONFIRMED, RSN_RESTORED);
            r->verify_fail = 0;
        } else {
            r->verify_fail++;
            set_state(in, BR_RESTORE, RSN_NONE);
        }
        break;
    default:
        set_state(in, BR_CONFIRMED, RSN_NONE);  // bootrec_read never returns another state
        break;
    }
    if (unsure(in))
        return INSTALL_REBOOT;  // this step's decision rested on an image that could not be read
    if (!put(in))
        return rec_failed(in);
    return CONTINUE;
}

// ---- the decision table -------------------------------------------------------------------

static void begin(install_t *in)
{
    in->vq = in->vb = in->vs = -1;
    in->read_error = false;
    in->degraded = false;
    in->prev_degraded = in->ho->degraded;
    in->ho->degraded = 0;
    handoff_store(in->ho);
}

static install_result_t decide(install_t *in)
{
    for (int guard = 0; guard < 64; guard++) {
        if (!vq(in) && !vb(in) && !vs(in))
            return nothing_valid(in);
        bootrec_t *r = &in->rec;
        bool exhausted = r->attempts > 3 || r->verify_fail >= 2;
        if (r->state == BR_BACKUP && r->attempts > 3) {
            in->rows |= INSTALL_ROW(7);
            set_state(in, BR_CONFIRMED, RSN_BACKUP_ABORTED);  // BACKUP only read QSPI: it is intact
            r->verify_fail = 0;
            if (!put(in))
                return rec_failed(in);
            continue;
        }
        if (r->state == BR_WRITE && exhausted && vb(in)) {
            in->rows |= INSTALL_ROW(8);
            enter_restore(in, RSN_WRITE_EXHAUSTED);
            if (!put(in))
                return rec_failed(in);
            continue;
        }
        if ((r->state == BR_WRITE && exhausted) || (r->state == BR_RESTORE && exhausted)) {
            in->rows |= INSTALL_ROW(9);
            return INSTALL_RESCUE_RETRY;
        }
        in->rows |= INSTALL_ROW(10);
        int res = step(in);
        if (res != CONTINUE)
            return (install_result_t)res;
    }
    in->rows |= INSTALL_BUG;
    return INSTALL_RESCUE_HALT;
}

install_result_t install_run(install_t *in)
{
    begin(in);
    int rr = bootrec_read(in->spi, &in->rec);
    if (rr < 0)
        return rec_failed(in);  // the record cannot be read: rows 3-6, never "no record"
    if (rr == BOOTREC_NONE) {
        // First boot after SWD flashing, or both records damaged (spec §11.2).
        memset(&in->rec, 0, sizeof in->rec);
        if (vq(in)) {
            set_state(in, BR_CONFIRMED, RSN_FIRST_BOOT);
        } else if (vb(in)) {
            enter_restore(in, RSN_FIRST_BOOT);
        } else if (vs(in)) {
            in->rows |= INSTALL_ROW(9);
            return INSTALL_RESCUE_RETRY;
        } else {
            return nothing_valid(in);
        }
        if (unsure(in))
            return INSTALL_REBOOT;
        if (!put(in))
            return rec_failed(in);
    }
    return decide(in);
}

install_result_t install_rescue_retry(install_t *in)
{
    begin(in);
    in->ho->ram_fail = 0;
    handoff_store(in->ho);
    int rr = bootrec_read(in->spi, &in->rec);
    if (rr < 0)
        return rec_failed(in);
    if (rr == BOOTREC_NONE)
        memset(&in->rec, 0, sizeof in->rec);
    if (vb(in)) {
        enter_restore(in, RSN_RESCUE_RETRY);
    } else if (vs(in)) {
        memcpy(in->rec.staging, in->hs.raw, IMG_CORE);
        set_state(in, BR_WRITE, RSN_RESCUE_RETRY);
        in->rec.verify_fail = 0;
    } else if (vq(in)) {
        set_state(in, BR_CONFIRMED, RSN_RESCUE_RETRY);  // no source to recover from, but QSPI is good
        in->rec.verify_fail = 0;
    } else {
        return nothing_valid(in);
    }
    // A record that cannot be written goes through rows 3-6 like anywhere else: a valid QSPI is
    // run degraded (never erased), rec_fail is counted, and only then the RAM path.
    if (!put(in))
        return rec_failed(in);
    return decide(in);
}
