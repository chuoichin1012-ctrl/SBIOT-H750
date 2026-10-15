// The network manager (spec §4, §7.1). Plan 06a: Ethernet only; LTE comes in Plan 09.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "eth_drv.h"
#include "netcfg.h"

typedef struct {
    unsigned used, max, err, size;  // size: elements (bytes for the heap)
} net_pool_t;

typedef struct {
    eth_status_t eth;
    bool added;                 // the netif exists (the ETH and the PHY came up)
    bool dhcp, bound;           // DHCP in use; an address held (static, or leased)
    uint32_t ip, mask, gw;      // running values, host order
    uint32_t dns1, dns2;
    uint8_t mac[6];
    net_pool_t heap, netconn, tcp_pcb, tcp_seg, pbuf_pool, udp_pcb;  // MEMP_STATS / MEM_STATS
} net_status_t;

/**
 * Starts lwIP on the Ethernet with these settings, read once at boot (spec §9.1). Waits for the
 * tcpip thread to set the netif up. 0, or a negative code when the network stays down (logged).
 */
int netmgr_init(const net_cfg_t *cfg, const uint8_t mac[6]);
/** The running state, copied under the lwIP core lock. */
void netmgr_status(net_status_t *out);
