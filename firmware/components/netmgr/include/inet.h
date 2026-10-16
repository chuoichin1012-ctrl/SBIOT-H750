// The Internet check's verdict per link (Plan 10 ruling 14). Pure C: netmgr's probe feeds it once a round.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define INET_MISSES 3  // rounds in a row without a reply: no Internet

typedef struct {
    bool ok;         // taken to have Internet until it fails
    uint8_t misses;  // rounds in a row without a reply
} inet_t;

void inet_init(inet_t *s);
/** One round: replied, or not. */
void inet_result(inet_t *s, bool replied);
