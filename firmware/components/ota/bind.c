#include "bind.h"
#include <string.h>
#include "crc32.h"
#include "flashmap.h"
#include "monocypher-ed25519.h"

static const uint8_t MAGIC[4] = {'S', 'B', 'B', '2'};
static const uint8_t MAGIC_V1[4] = {'S', 'B', 'B', 'D'};
static const char LABEL[16] __attribute__((nonstring)) = "SBIOT-H750 bind2";  // no NUL
#define SIGNED_AT 4u    // UID, MAC, SN
#define SIGNED_LEN 30u
#define MAC_AT 16u
#define SN_AT 22u
#define SIG_AT 36u
#define CRC_AT 100u

static bool crc_ok(const uint8_t rec[BIND_LEN])
{
    uint32_t c;
    memcpy(&c, rec + CRC_AT, 4);
    return crc32_update(0, rec, CRC_AT) == c;
}

bool bind_valid(const uint8_t rec[BIND_LEN], const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN])
{
    if (memcmp(rec, MAGIC, sizeof MAGIC) || !crc_ok(rec))
        return false;
    // The record's UID must be this chip's, and the signature covers it with the MAC and the SN (Plan 11a).
    if (memcmp(rec + SIGNED_AT, uid, BIND_UID_LEN))
        return false;
    uint8_t msg[sizeof LABEL + SIGNED_LEN];
    memcpy(msg, LABEL, sizeof LABEL);
    memcpy(msg + sizeof LABEL, rec + SIGNED_AT, SIGNED_LEN);
    return crypto_ed25519_check(rec + SIG_AT, pub, msg, sizeof msg) == 0;
}

bool bind_identity(const uint8_t rec[BIND_LEN], uint8_t mac[6], char sn[13])
{
    const uint8_t *s = rec + SN_AT;
    if (s[0] != 'S' || s[1] != 'B')
        return false;
    for (int i = 2; i < 12; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    memcpy(mac, rec + MAC_AT, 6);
    memcpy(sn, s, 12);
    sn[12] = '\0';
    return true;
}

bind_result_t bind_check(nor_t *qspi, const uint8_t pub[BIND_PUB_LEN], const uint8_t uid[BIND_UID_LEN], void (*kick)(void))
{
    uint8_t rec[BIND_LEN];
    if (qspi->read(qspi, QSPI_BIND, rec, BIND_LEN))
        return BIND_EREAD;
    if (!memcmp(rec, MAGIC_V1, sizeof MAGIC_V1))
        return BIND_EOLD;  // programmed before Plan 11a: no MAC or SN; nothing erased (the user)
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
