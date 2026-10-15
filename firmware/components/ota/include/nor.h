// A NOR flash as the OTA code sees it (spec §5.2, §11.2): byte reads, 4 KB sector erase, programming
// within one 256-byte page. Implemented by the QSPI and SPI4 drivers on the board and by
// test/host/nor_sim.c on the host. Every wait inside an implementation has a deadline of twice
// the datasheet maximum (GD25Q32E Rev 1.3 §8.6: tSE 300 ms, tPP 2.4 ms) and kicks the watchdogs.
#pragma once
#include <stdint.h>

#define NOR_SECTOR 4096u
#define NOR_PAGE 256u

enum {
    NOR_OK = 0,
    NOR_TIMEOUT = -1,  // the device did not finish within its deadline
    NOR_EIO = -2,      // bus or controller error
};

typedef struct nor nor_t;
struct nor {
    int (*read)(nor_t *n, uint32_t off, void *buf, uint32_t len);
    int (*erase_sector)(nor_t *n, uint32_t off);                           // off is 4 KB aligned
    int (*program)(nor_t *n, uint32_t off, const void *buf, uint32_t len);  // within one page
    void *ctx;
};
