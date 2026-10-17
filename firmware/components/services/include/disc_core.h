// SBIOT Finder's packets on the gateway (Plan 12; stm32/docs/specs/2026-10-17-lan-discovery-design.md §2): parse and
// check a datagram, build the `found` and `ack` replies. Pure C, host-tested (test_disc_core).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define DISC_PORT 48600
#define DISC_MAX 512  // a datagram's limit; the task reads DISC_MAX + 1 to see one too long

typedef enum { DISC_IGNORE, DISC_SEARCH, DISC_SET, DISC_RESTART, DISC_FACTORY, DISC_BAD } disc_kind_t;

typedef struct {
    int ip_mode, dns_mode;                             // 0 or 1
    char ip[16], netmask[16], gateway[16], dns1[16], dns2[16];
} disc_net_t;

typedef struct {
    char id[9];
    disc_net_t net;  // DISC_SET only
} disc_req_t;

/** What the gateway with this MAC ("02:95:b8:e9:a7:9f") does with one datagram (spec §2.1-§2.3). DISC_BAD: a
 *  `set` for this gateway whose `net` is wrong (answer `ack` err "bad"); out->id is filled for every kind but
 *  DISC_IGNORE. */
disc_kind_t disc_parse(const char *buf, size_t len, const char *my_mac, disc_req_t *out);

typedef struct {
    const char *mac, *sn, *model, *fw, *hostname;
    unsigned web;
    uint32_t boot;
    uint32_t eth_ip, eth_mask, eth_gw;  // running, host order
    disc_net_t saved;                   // the seven saved fields
} disc_info_t;

/** The `found` reply; its length, or -1 when it does not fit cap. */
int disc_found(const disc_info_t *in, const char *id, char *buf, size_t cap);
/** The `ack`: err NULL for ok, else "bad" or "store". Its length, or -1. */
int disc_ack(const char *id, const char *mac, const char *err, char *buf, size_t cap);
/** The seven fields of a `network` config object (a missing field reads 0 or ""). */
void disc_net_from(const cJSON *network, disc_net_t *out);
/** The seven fields as an object for store_patch; NULL without memory. */
cJSON *disc_net_json(const disc_net_t *n);
