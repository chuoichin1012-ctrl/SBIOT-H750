// The modem's AT replies (spec §7.2, Plan 10): final results, the identity, the signal. Pure C.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { AT_NONE, AT_OK, AT_ERROR, AT_CONNECT, AT_NO_CARRIER, AT_POWERED_DOWN } at_final_t;

/** What a whole line ends: "OK", "ERROR", "+CME ERROR: n", "+CMS ERROR: n", "CONNECT[ x]", "NO CARRIER",
 *  "POWERED DOWN"; AT_NONE for any other line. */
at_final_t at_final(const char *line);
bool at_imei(const char *line, char out[16]);           // 15 digits alone on their line (AT+CGSN)
bool at_iccid(const char *line, char out[21]);          // "+QCCID: 8984…F": up to 20 hex digits
bool at_csq(const char *line, int *rssi);               // "+CSQ: 18,99": 18 (99 unknown)
bool at_nwinfo(const char *line, char *out, size_t n);  // "+QNWINFO: \"FDD LTE\",…": "LTE"; others as given
bool at_cpin_ready(const char *line);                   // "+CPIN: READY"

/** Lines from a byte stream: a whole line (CR and LF stripped, at least one character) or NULL. A line over
 *  the buffer is cut at its size. */
typedef struct {
    char line[128];
    size_t n;
} at_lines_t;
const char *at_feed(at_lines_t *l, uint8_t c);
