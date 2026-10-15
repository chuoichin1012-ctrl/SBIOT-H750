// Host tests for the IP101 PHY driver (spec §7.1): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ip101.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

// A fake MDIO bus: 32 addresses of 32 registers. An absent address reads 0xFFFF, as an MDIO line
// with its pull-up does. BMSR's link bit latches low until read, as clause 22 says.
typedef struct {
    uint16_t reg[32][32];
    int present[32];
    int link_now;         // the line state behind the latched bit
    int link_latched_low;
    int fail_read_at;     // the address whose reads fail, or -1
    int fail_write;
    int reads;
} bus_t;

static int bus_read(void *ctx, uint32_t addr, uint32_t reg, uint32_t *val)
{
    bus_t *b = ctx;
    b->reads++;
    if ((int)addr == b->fail_read_at)
        return -1;
    if (!b->present[addr]) {
        *val = 0xFFFF;
        return 0;
    }
    uint16_t v = b->reg[addr][reg];
    if (reg == PHY_BMSR) {
        v &= (uint16_t)~BMSR_LINK;
        if (b->link_now && !b->link_latched_low)
            v |= BMSR_LINK;
        b->link_latched_low = 0;  // a read clears the latch
    }
    *val = v;
    return 0;
}

static int bus_write(void *ctx, uint32_t addr, uint32_t reg, uint32_t val)
{
    bus_t *b = ctx;
    if (b->fail_write)
        return -1;
    b->reg[addr][reg] = (uint16_t)(val & (uint16_t)~BMCR_ANRESTART);  // self-clearing
    if (reg == PHY_BMCR && (val & BMCR_PDOWN))
        b->link_now = 0;
    return 0;
}

static void bus_with_phy(bus_t *b, uint32_t addr)
{
    memset(b, 0, sizeof *b);
    b->fail_read_at = -1;
    b->present[addr] = 1;
    b->reg[addr][PHY_ID1] = 0x0243;
    b->reg[addr][PHY_ID2] = 0x0C54;
    b->reg[addr][PHY_BMCR] = BMCR_ANEN | BMCR_SPEED100 | BMCR_FULL;
    b->reg[addr][PHY_BMSR] = 0x7809 | BMSR_ANDONE;
    b->reg[addr][PHY_ANAR] = AN_100FULL | AN_100HALF | AN_10FULL | AN_10HALF | 1;
    b->reg[addr][PHY_ANLPAR] = AN_100FULL | AN_100HALF | AN_10FULL | AN_10HALF | 1;
    b->link_now = 1;
}

static ip101_io_t io_of(bus_t *b)
{
    return (ip101_io_t){.read = bus_read, .write = bus_write, .ctx = b};
}

static void test_scan(void)
{
    bus_t b;
    ip101_t phy;
    bus_with_phy(&b, 1);
    ip101_io_t io = io_of(&b);
    CHECK(ip101_init(&phy, &io) == IP101_OK && phy.addr == 1 && phy.id == 0x02430C54u,
          "found at 1 with its ID (addr %u id %08x)", (unsigned)phy.addr, (unsigned)phy.id);

    bus_with_phy(&b, 0);
    b.present[0] = 0;
    CHECK(ip101_init(&phy, &io) == IP101_ENOPHY, "no PHY at any address");

    bus_with_phy(&b, 5);
    b.reg[5][PHY_ID1] = 0x0000;
    CHECK(ip101_init(&phy, &io) == IP101_ENOPHY, "an ID of 0x0000 is not a PHY");

    bus_with_phy(&b, 3);
    b.fail_read_at = 1;
    CHECK(ip101_init(&phy, &io) == IP101_OK && phy.addr == 3, "a read error at one address: the scan goes on");

    bus_with_phy(&b, 0);
    b.fail_read_at = 0;
    CHECK(ip101_init(&phy, &io) == IP101_EREAD, "nothing found and a read failed: a read error");
}

static ip101_status_t link_with(bus_t *b, uint16_t bmcr, uint16_t bmsr_extra, uint16_t lp)
{
    ip101_t phy;
    ip101_io_t io = io_of(b);
    if (ip101_init(&phy, &io) != IP101_OK)
        return IP101_ENOPHY;
    b->reg[phy.addr][PHY_BMCR] = bmcr;
    b->reg[phy.addr][PHY_BMSR] = (uint16_t)(0x7809 | bmsr_extra);
    b->reg[phy.addr][PHY_ANLPAR] = lp;
    return ip101_link(&phy);
}

static void test_link(void)
{
    bus_t b;
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_100FULL | AN_10HALF) == IP101_100_FULL, "100 full");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_100HALF | AN_10FULL) == IP101_100_HALF, "100 half beats 10 full");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_10FULL | AN_10HALF) == IP101_10_FULL, "10 full");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, 0) == IP101_10_HALF, "no common ability: 10 half");
    bus_with_phy(&b, 1);
    b.reg[1][PHY_ANAR] = AN_10FULL | AN_10HALF | 1;  // we advertise only 10 Mbit
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_100FULL | AN_10FULL) == IP101_10_FULL, "only what both advertise");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_ANEN, 0, AN_100FULL) == IP101_AUTONEG_NOT_DONE, "autoneg not done");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_SPEED100, 0, 0) == IP101_100_HALF, "autoneg off: forced 100 half from BMCR");
    bus_with_phy(&b, 1);
    CHECK(link_with(&b, BMCR_FULL, 0, 0) == IP101_10_FULL, "autoneg off: forced 10 full from BMCR");

    bus_with_phy(&b, 1);
    b.link_now = 0;
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_100FULL) == IP101_LINK_DOWN, "link down");

    // A drop latched since the last read, the line up again: the second read tells the state now.
    bus_with_phy(&b, 1);
    b.link_latched_low = 1;
    CHECK(link_with(&b, BMCR_ANEN, BMSR_ANDONE, AN_100FULL) == IP101_100_FULL, "a latched drop: BMSR read twice");

    ip101_t phy;
    bus_with_phy(&b, 1);
    ip101_io_t io = io_of(&b);
    ip101_init(&phy, &io);
    b.fail_read_at = 1;
    CHECK(ip101_link(&phy) == IP101_EREAD, "a read error is reported, not taken for a link state");
}

static void test_power(void)
{
    bus_t b;
    ip101_t phy;
    bus_with_phy(&b, 2);
    ip101_io_t io = io_of(&b);
    ip101_init(&phy, &io);
    CHECK(ip101_power(&phy, 1) == IP101_OK && (b.reg[2][PHY_BMCR] & BMCR_PDOWN), "power down sets BMCR.PD");
    CHECK(ip101_link(&phy) == IP101_LINK_DOWN, "powered down: link down");
    CHECK(ip101_power(&phy, 0) == IP101_OK && !(b.reg[2][PHY_BMCR] & BMCR_PDOWN) && (b.reg[2][PHY_BMCR] & BMCR_ANEN),
          "power up clears PD, autoneg on");
    b.fail_write = 1;
    CHECK(ip101_power(&phy, 1) == IP101_EWRITE, "a write error is reported");
}

int main(void)
{
    test_scan();
    test_link();
    test_power();
    printf("test_ip101: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
