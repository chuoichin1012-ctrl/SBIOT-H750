#include "bind.h"
#include <string.h>
#include "crc32.h"
#include "flashmap.h"
#include "sha256.h"

static const uint8_t MAGIC[4] = {'S', 'B', 'B', 'D'};
static const char LABEL[15] = "SBIOT-H750 bind";  // no NUL

void bind_record(const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN], uint8_t rec[BIND_LEN])
{
    uint8_t msg[sizeof LABEL + BIND_UID_LEN];
    memcpy(msg, LABEL, sizeof LABEL);
    memcpy(msg + sizeof LABEL, uid, BIND_UID_LEN);
    memcpy(rec, MAGIC, sizeof MAGIC);
    hmac_sha256(key, BIND_KEY_LEN, msg, sizeof msg, rec + 4);
    uint32_t c = crc32_update(0, rec, 36);
    memcpy(rec + 36, &c, 4);
}

bool bind_matches(const uint8_t rec[BIND_LEN], const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN])
{
    uint8_t want[BIND_LEN];
    bind_record(key, uid, want);
    return !memcmp(rec, want, BIND_LEN);
}

// Erases the sector, programs tag and CRC, reads them back, and only then programs the magic: a
// record cut short or programmed wrong never has a magic, so the next boot writes it again.
static bind_result_t write_record(nor_t *q, const uint8_t want[BIND_LEN])
{
    uint8_t back[BIND_LEN];
    if (q->erase_sector(q, QSPI_BIND) || q->program(q, QSPI_BIND + 4, want + 4, BIND_LEN - 4))
        return BIND_EWRITE;
    if (q->read(q, QSPI_BIND, back, BIND_LEN) || memcmp(back + 4, want + 4, BIND_LEN - 4))
        return BIND_EWRITE;
    if (q->program(q, QSPI_BIND, want, sizeof MAGIC))
        return BIND_EWRITE;
    if (q->read(q, QSPI_BIND, back, BIND_LEN) || memcmp(back, want, BIND_LEN))
        return BIND_EWRITE;
    return BIND_WRITTEN;
}

bind_result_t bind_check(nor_t *qspi, const uint8_t key[BIND_KEY_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void))
{
    uint8_t rec[BIND_LEN], want[BIND_LEN];
    bind_record(key, uid, want);
    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))
        return BIND_EREAD;
    if (memcmp(rec, MAGIC, sizeof MAGIC))
        return write_record(qspi, want);  // first boot, or a write cut short
    uint32_t c;
    memcpy(&c, rec + 36, 4);
    if (crc32_update(0, rec, 36) != c)
        return BIND_ECORRUPT;
    if (!memcmp(rec + 4, want + 4, 32))
        return BIND_OK;
    // A valid record of another chip or key: this image was copied. Erase the application, keep
    // the record so that every later boot refuses too. Erase errors do not stop the others.
    for (uint32_t s = 0; s < SLOT_SIZE; s += NOR_SECTOR) {
        qspi->erase_sector(qspi, QSPI_SLOT + s);
        if (kick)
            kick();
    }
    return BIND_CLONE;
}
