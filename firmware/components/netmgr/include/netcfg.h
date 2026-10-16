// The network settings, from the `network` and `system` configs (spec §7.1, §7.2). Read once at boot: a saved
// change applies after a restart (spec §9.1). Pure C, host-tested (test_netcfg).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#define NETCFG_HOSTNAME_DEFAULT "sbiot-gw"

typedef struct {
    bool dhcp;              // false: the static address below
    bool static_incomplete; // ipMode asked for a static address, but ip, netmask or gateway was not valid
    uint32_t ip, mask, gw;  // host order (192.168.0.7 is 0xC0A80007); 0 when DHCP
    bool dns_manual;        // dns1/dns2 below; otherwise DHCP's
    uint32_t dns1, dns2;    // 0: none
    char hostname[17];      // system.hostname (at most 16), NETCFG_HOSTNAME_DEFAULT when empty
    // LTE (Plan 10)
    int priority;           // 0 Ethernet only, 1 Ethernet first, 2 LTE first
    char apn[31], apn_user[31], apn_pass[31];  // apn "internet" when empty, as the ESP32
    int apn_auth;           // 0 none, 1 PAP, 2 CHAP, 3 either
    bool cell_dns_manual;   // cellDnsMode 1: cell_dns1/cell_dns2; otherwise PPP's
    uint32_t cell_dns1, cell_dns2;
} net_cfg_t;

typedef enum { NET_ROUTE_NONE, NET_ROUTE_ETH, NET_ROUTE_CELL } net_route_t;
/** The default route for network.priority and the links that hold an address. */
net_route_t netcfg_pick(int priority, bool eth_ok, bool cell_ok);
/** As netcfg_pick, with the Internet check (Plan 10 ruling 14): a link with an address but no Internet is left
 *  only for the other one when that one has both; when neither has Internet, by address alone. */
net_route_t netcfg_pick_inet(int priority, bool eth_ok, bool eth_inet, bool cell_ok, bool cell_inet);

/** Each link's DNS servers from their own sources (Plan 10 ruling 5): the config when manual, otherwise the
 *  DHCP lease's (Ethernet) or the ones PPP negotiated (the cell); 0 for none. */
typedef struct {
    bool eth_manual, cell_manual;
    uint32_t eth_cfg[2], cell_cfg[2];
    uint32_t eth_lease[2], cell_peer[2];
} net_dns_t;
/** The pair lwIP's two servers take for this route (Ethernet's when there is none). */
void netcfg_dns(const net_dns_t *d, net_route_t route, uint32_t out[2]);

/**
 * The ESP32's rules (firmware/components/netmgr): ipMode 1 is static; a static address always
 * uses the manual DNS; an incomplete static address falls back to DHCP, and the DNS then follows
 * dnsMode (1 manual). A missing object or field reads as 0 or empty.
 */
void netcfg_from_json(const cJSON *network, const cJSON *system, net_cfg_t *out);

/** Strict dotted quad: four decimal numbers 0-255, no sign, space or trailing text. */
bool netcfg_ip4(const char *s, uint32_t *out);

/** 02 (locally administered, unicast), then the first 5 bytes of SHA-256 over the 12-byte UID. */
void netcfg_mac(const uint8_t uid[12], uint8_t mac[6]);
