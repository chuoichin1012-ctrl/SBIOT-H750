// The Ethernet settings, from the `network` and `system` configs (spec §7.1). Read once at boot: a saved
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
} net_cfg_t;

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
