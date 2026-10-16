// The network manager (spec §4, §7.1, §7.2): Ethernet, and the LTE link with the route and DNS choice (Plan 10).
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
/** The link is up and an address is held: the bridges may dial. */
bool netmgr_online(void);
/** Netconns free in lwIP's pool (spec §5.6: the TCP servers' accept gate). */
int netmgr_netconn_free(void);
struct netif;
/** PPP's status (lte.c, in the tcpip thread): the cell link up with the DNS IPCP gave (0 for none), or down. The
 *  route and the DNS are chosen again. */
void netmgr_cell(struct netif *nif, bool up, const uint32_t dns[2]);
/** "ETH", "LTE" or "NONE": the link holding the default route. */
const char *netmgr_route_name(void);
/** The Internet check per link (Plan 10 ruling 14): an address and replies to the probe. */
void netmgr_internet(bool *eth, bool *cell);
