// Allowed-client list for the serial servers (TCP server / UDP server):
// IPv4 addresses and CIDR ranges, e.g. "192.168.1.0/24, 10.0.0.5".
// Pure C, no ESP-IDF: host-tested in firmware/test/host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IPACL_MAX 8

typedef struct {
    int n;  // 0 = anyone may connect
    struct {
        uint32_t net, mask;  // host byte order
    } r[IPACL_MAX];
} ipacl_t;

/**
 * Parses a list separated by commas, semicolons or spaces. An empty list
 * allows anyone. Returns false (and n = 0) on a malformed entry or more
 * than IPACL_MAX entries.
 */
bool ipacl_parse(const char *s, ipacl_t *out);
/** True when `ip` (host byte order) is allowed. */
bool ipacl_allows(const ipacl_t *acl, uint32_t ip);
