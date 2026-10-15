#include "bind.h"
#include <string.h>
#include "crc32.h"
#include "flashmap.h"
#include "monocypher-ed25519.h"

static const uint8_t MAGIC[4] = {'S', 'B', 'B', 'D'};
static const char LABEL[15] __attribute__((nonstring)) = "SBIOT-H750 bind";  // no NUL

static bool crc_ok(const uint8_t rec[BIND_LEN])
{
    uint32_t c;
    memcpy(&c, rec + 80, 4);
    return crc32_update(0, rec, 80) == c;
}

bool bind_valid(const uint8_t rec[BIND_LEN], const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN])
{
    if (memcmp(rec, MAGIC, sizeof MAGIC) || !crc_ok(rec))
        return false;
    // Checked over this chip's UID; the UID stored in the record is only for people reading it.
    uint8_t msg[sizeof LABEL + BIND_UID_LEN];
    memcpy(msg, LABEL, sizeof LABEL);
    memcpy(msg + sizeof LABEL, uid, BIND_UID_LEN);
    return crypto_ed25519_check(rec + 16, pub, msg, sizeof msg) == 0;
}

bind_result_t bind_check(nor_t *qspi, const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void))
{
    uint8_t rec[BIND_LEN];
    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))
        return BIND_EREAD;
    if (memcmp(rec, MAGIC, sizeof MAGIC))
        return BIND_ENONE;  // never provisioned: the bootloader never makes a record itself
    if (!crc_ok(rec))
        return BIND_ECORRUPT;  // treated as damage (a forger must fix the CRC): nothing is erased
    if (bind_valid(rec, pub, uid))
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
