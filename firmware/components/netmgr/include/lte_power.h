// The modem's power sequence and recovery decisions (spec §7.2, Plan 10). Pure C: the hardware comes as
// callbacks. The EC600G-EU takes the EC600U's timings (Plan 10 ruling 13): if the bench finds one wrong, it
// changes here, in one place.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LTE_PROBE_MS 3000      // AT at the start: the module is often still on after a gateway restart
#define LTE_RESET_MS 200       // RESET_N (>= 100 ms)
#define LTE_PWRKEY_ON_MS 2500  // PWRKEY: >= 2 s turns on; >= 3 s would turn a running module off
#define LTE_PWRKEY_LIMIT_MS 3000  // never reached, on any path
#define LTE_READY_MS 10000     // AT tried every 500 ms after a release: 2 x the bench (1.0 s after RESET_N, at once after PWRKEY), not under 10 s (spec §7.2)
#define LTE_RETRY_MS 60000     // MODEM NOT RESPONDING: the wait before the next round
#define LTE_NO_ADDR_MS 120000  // PPP without an address this long: AT+CFUN=1,1
#define LTE_SILENT_MS 60000    // no frame from the modem this long, and AT silent: RECOVER

typedef enum { LTE_UNKNOWN, LTE_ON, LTE_OFF } lte_power_t;
enum { LTE_PIN_PWRKEY, LTE_PIN_RESET };

typedef struct {
    bool (*at_ok)(void *ctx, uint32_t window_ms);  // AT tried every 500 ms until it answers or the window ends
    void (*pulse)(void *ctx, int pin, uint32_t ms);
    void (*log)(void *ctx, const char *msg);
    void *ctx;
} lte_hw_t;

/**
 * The start sequence: LTE_ON once AT answers, or LTE_UNKNOWN after a whole round (logged as MODEM NOT
 * RESPONDING; the caller waits LTE_RETRY_MS and calls again from LTE_UNKNOWN).
 * - from LTE_UNKNOWN: AT for 3 s; RESET_N, AT for 15 s; PWRKEY 2.5 s, AT for 15 s;
 * - from LTE_OFF (POWERED DOWN seen): PWRKEY 2.5 s, AT for 15 s; then as from UNKNOWN's RESET_N.
 * PWRKEY is pressed only after RESET_N did not make AT answer, or from a confirmed OFF.
 */
lte_power_t lte_power_up(const lte_hw_t *hw, lte_power_t from);

typedef enum { LTE_ACT_NONE, LTE_ACT_CFUN, LTE_ACT_RECOVER } lte_action_t;
/** Supervision: no frame for LTE_SILENT_MS with AT silent is RECOVER, SIM or not; otherwise no SIM never acts;
 *  no address for LTE_NO_ADDR_MS is CFUN. */
lte_action_t lte_supervise(uint32_t now_ms, uint32_t last_addr_ms, uint32_t last_frame_ms, bool at_alive, bool no_sim);
