// FAL flash device over a nor_t (nor.h): FlashDB's 1 MB at the device's offset 0. Writes are split
// into page programs, erases into 4 KB sectors. FAL treats negative results as errors. The lock is
// the store's (FlashDB is only called with it held).
#include <fal.h>
#include "fal_nor.h"

#define FDB_REGION 0x100000u

static nor_t *s_nor;

static bool s_read_failed;

void fal_nor_set(nor_t *nor) { s_nor = nor; }
bool fal_nor_read_failed(void) { return s_read_failed; }
void fal_nor_rearm(void) { s_read_failed = false; }

static int dev_init(void) { return s_nor ? 0 : -1; }

// FlashDB hardly handles a failed read (a sector header that does not read back can even be
// formatted at mount), and a read has no side effect: a transient bus fault is retried here.
#define READ_TRIES 3

static int dev_read(long off, uint8_t *buf, size_t size)
{
    if (!s_nor || off < 0 || (uint32_t)off + size > FDB_REGION)
        return -1;
    for (int i = 0; i < READ_TRIES; i++)
        if (!s_nor->read(s_nor, (uint32_t)off, buf, (uint32_t)size))
            return (int)size;
    s_read_failed = true;  // read-only from now on: FlashDB must not act on what it did not read
    return -1;
}

static int dev_write(long off, const uint8_t *buf, size_t size)
{
    if (!s_nor || s_read_failed || off < 0 || (uint32_t)off + size > FDB_REGION)
        return -1;
    for (size_t done = 0; done < size;) {
        uint32_t at = (uint32_t)off + (uint32_t)done;
        uint32_t n = NOR_PAGE - at % NOR_PAGE;
        if (n > size - done)
            n = (uint32_t)(size - done);
        if (s_nor->program(s_nor, at, buf + done, n)) {
            s_read_failed = true;  // locked before FlashDB hears of it: it may go on using that sector (Plan 09b T2)
            return -1;
        }
        done += n;
    }
    return (int)size;
}

static int dev_erase(long off, size_t size)
{
    if (!s_nor || s_read_failed || off < 0 || off % NOR_SECTOR || (uint32_t)off + size > FDB_REGION)
        return -1;
    for (size_t done = 0; done < size; done += NOR_SECTOR)
        if (s_nor->erase_sector(s_nor, (uint32_t)off + (uint32_t)done)) {
            s_read_failed = true;
            return -1;
        }
    return (int)size;
}

const struct fal_flash_dev store_fal_dev = {
    .name = STORE_FAL_DEV,
    .addr = 0,
    .len = FDB_REGION,
    .blk_size = NOR_SECTOR,
    .ops = {dev_init, dev_read, dev_write, dev_erase},
    .write_gran = 1,
};
