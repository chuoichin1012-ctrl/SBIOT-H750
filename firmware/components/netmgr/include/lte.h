// The LTE modem (spec §7.2, Plan 10): the EC600G-EU on UART3, its power, AT, CMUX, PPP and supervision. Two
// tasks: `cmux_rx` (prio 25) owns UART3 and the CMUX state, `lte` (prio 5) runs the sequence. They talk through
// stream buffers only.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "netcfg.h"

typedef struct {
    char imei[16], iccid[21], fw[48], net_type[24];
    int csq;            // 99: unknown
    const char *state;  // "disconnected", "connecting", "connected" (the UI's values)
    const char *modem;  // "ON", "OFF", "UNKNOWN"
    const char *mux;    // "AT", "OPENING", "RUNNING", "PLAIN", "FAILED"
    const char *sim;    // "", "READY", "NO SIM"
    uint32_t ip, dns1, dns2;
    uint32_t ready_ms;  // measured: from a release (PWRKEY or RESET_N) to AT answering (Plan 10 ruling 13)
    uint32_t overrun, bad_fcs, ppp_in_drop, ppp_out_drop, uart_errors;
    uint8_t last_bad[6];  // bench: the last frame with a bad FCS (cmux_counters_t)
} lte_info_t;

/** Starts UART3 and both tasks (priority 1 or 2); priority 0 leaves the modem alone (Plan 10 T5 powers it down). */
void lte_start(const net_cfg_t *cfg);
void lte_info(lte_info_t *out);
/** Before a gateway restart: AT+QPOWD=1 and POWERED DOWN (up to timeout_ms, spec §7.2); true at once when the
 *  module is not ON. PWRKEY is never pressed. */
bool lte_shutdown(uint32_t timeout_ms);
/** Bench: DISC on DLC2, which the lte task recovers from (Plan 10 T5). */
void lte_bench_disc(void);
/** Bench: PPP's input dropped (toggle), so PPP never gets an address: the CFUN case. The new state. */
bool lte_bench_drop(void);
/** Bench: text and CR out of UART3 as they are (AT mode). */
void lte_send_raw(const char *text);
