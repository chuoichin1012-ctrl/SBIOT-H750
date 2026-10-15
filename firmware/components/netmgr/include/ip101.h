// IP101GRI PHY over MDIO (spec §7.1). Shaped after ST's lan8742 component (STM32Cube H7,
// Drivers/BSP/Components/lan8742): an object holding the bus IO, the address found by a scan,
// and one status code per link state. It differs where the parts differ:
// - the IP101 has no SMR register, so the scan looks for a valid PHY ID (registers 2 and 3), and
//   tries address 0 last, since the IP101 answers there too (broadcast);
// - speed and duplex come from the IEEE registers (BMCR, then BMSR and ANAR & ANLPAR), not a
//   vendor register.
// Pure C: the MDIO access is the caller's (HAL_ETH_ReadPHYRegister on the board, a fake in test_ip101).
#pragma once
#include <stdint.h>

typedef struct {
    int (*read)(void *ctx, uint32_t addr, uint32_t reg, uint32_t *val);  // 0 on success
    int (*write)(void *ctx, uint32_t addr, uint32_t reg, uint32_t val);
    void *ctx;
} ip101_io_t;

typedef struct {
    ip101_io_t io;
    uint32_t addr;  // MDIO address, valid after ip101_init answered IP101_OK
    uint32_t id;    // ID1 << 16 | ID2
} ip101_t;

typedef enum {
    IP101_EREAD = -3,
    IP101_EWRITE = -2,
    IP101_ENOPHY = -1,  // no PHY answered at any address
    IP101_OK = 0,
    IP101_LINK_DOWN,
    IP101_100_FULL,
    IP101_100_HALF,
    IP101_10_FULL,
    IP101_10_HALF,
    IP101_AUTONEG_NOT_DONE,
} ip101_status_t;

// IEEE 802.3 clause 22 registers and the bits used here.
#define PHY_BMCR 0u
#define PHY_BMSR 1u
#define PHY_ID1 2u
#define PHY_ID2 3u
#define PHY_ANAR 4u
#define PHY_ANLPAR 5u
#define BMCR_SPEED100 (1u << 13)
#define BMCR_ANEN (1u << 12)
#define BMCR_PDOWN (1u << 11)
#define BMCR_ANRESTART (1u << 9)
#define BMCR_FULL (1u << 8)
#define BMSR_ANDONE (1u << 5)
#define BMSR_LINK (1u << 2)
#define AN_100FULL (1u << 8)
#define AN_100HALF (1u << 7)
#define AN_10FULL (1u << 6)
#define AN_10HALF (1u << 5)

/** Finds the PHY: the lowest address from 1 to 31, then 0 (broadcast), whose ID registers read neither
 * 0x0000 nor 0xFFFF. */
ip101_status_t ip101_init(ip101_t *phy, const ip101_io_t *io);
/** The link now: down, autoneg pending, or the speed and duplex in use. */
ip101_status_t ip101_link(ip101_t *phy);
typedef enum {
    IP101_KEEP,      // nothing to do
    IP101_START,     // set the MAC to the new speed and duplex, start it, link up
    IP101_STOP,      // stop the MAC, link down
    IP101_RECONFIG,  // still up, at another speed or duplex: stop, set the MAC again, start
} ip101_action_t;

/**
 * What the MAC must do, from the state it was set up for (`applied`; IP101_LINK_DOWN while stopped)
 * and the state read now. A renegotiation between two polls changes the speed without a "down"
 * seen in between (Codex 06a review). A read error keeps everything as it is.
 */
ip101_action_t ip101_action(ip101_status_t applied, ip101_status_t now);
/** Powers the PHY down (the link drops), or up again with autoneg restarted. */
ip101_status_t ip101_power(ip101_t *phy, int down);
