// The H7 ETH and the IP101 PHY as an lwIP netif (spec §5.4, §6.1, §7.1). Shaped after ST's
// ethernetif.c (STM32Cube H7, LwIP_HTTP_Server_Netconn_RTOS), changed where the spec differs:
// TX copies into buffers of its own, RX is zero-copy from a pool of its own (both in SRAM1), and
// the eth_rx task hands lwIP at most 8 frames per wake and polls the link each second.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lwip/err.h"

struct netif;

typedef struct {
    bool up;            // ETH started, link up
    int speed;          // 10 or 100, 0 when down
    bool full_duplex;
    int phy_addr;       // -1: no PHY found
    uint32_t phy_id;
    unsigned rx_in_use;     // RX buffers lent to descriptors or to lwIP
    unsigned rx_starved;    // times the RX pool ran dry
    unsigned tx_busy_drops; // frames dropped: no TX buffer within 20 ms
    unsigned link_changes;
} eth_status_t;

/**
 * lwIP's init for netif_add (runs in the tcpip thread). netif->state points at the 6-byte MAC.
 * Resets the PHY, initialises the ETH, finds the PHY and starts eth_rx. ERR_IF when any of that
 * failed: the netif is not added, and the network stays down.
 */
err_t eth_drv_netif_init(struct netif *netif);
void eth_drv_status(eth_status_t *out);
/** Bench: powers the PHY down (the link drops) or up again. */
int eth_drv_phy_power(bool down);
