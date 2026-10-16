// The modem's power sequence and recovery decisions (spec §7.2, Plan 10). See lte_power.h.
#include "lte_power.h"

static bool reset_then_ask(const lte_hw_t *hw)
{
    hw->pulse(hw->ctx, LTE_PIN_RESET, LTE_RESET_MS);  // on: it restarts; off: harmless
    return hw->at_ok(hw->ctx, LTE_READY_MS);
}

static bool pwrkey_then_ask(const lte_hw_t *hw)
{
    hw->pulse(hw->ctx, LTE_PIN_PWRKEY, LTE_PWRKEY_ON_MS);  // >= 2 s on, < 3 s: a running module stays on
    return hw->at_ok(hw->ctx, LTE_READY_MS);
}

lte_power_t lte_power_up(const lte_hw_t *hw, lte_power_t from)
{
    bool ok;
    if (from == LTE_OFF)
        ok = pwrkey_then_ask(hw) || reset_then_ask(hw);
    else
        ok = hw->at_ok(hw->ctx, LTE_PROBE_MS) || reset_then_ask(hw) || pwrkey_then_ask(hw);
    if (ok)
        return LTE_ON;
    hw->log(hw->ctx, "MODEM NOT RESPONDING");  // not taken for OFF: the state stays UNKNOWN
    return LTE_UNKNOWN;
}

lte_action_t lte_supervise(uint32_t now_ms, uint32_t last_addr_ms, uint32_t last_frame_ms, bool at_alive, bool no_sim,
                           uint32_t no_addr_ms)
{
    if (now_ms - last_frame_ms >= LTE_SILENT_MS && !at_alive)
        return LTE_ACT_RECOVER;  // a silent module, SIM or not
    if (no_sim)
        return LTE_ACT_NONE;  // the module answers: no SIM is no reason to reset it
    if (now_ms - last_addr_ms >= no_addr_ms)
        return LTE_ACT_CFUN;
    return LTE_ACT_NONE;
}

uint32_t lte_backoff_limit(const lte_backoff_t *b)
{
    static const uint32_t minutes[LTE_BACKOFF_CAP + 1] = {2, 5, 10, 30};
    return minutes[b->fails < LTE_BACKOFF_CAP ? b->fails : LTE_BACKOFF_CAP] * 60000u;
}

lte_action_t lte_backoff_step(lte_backoff_t *b)
{
    lte_action_t a = b->fails < 2 ? LTE_ACT_CFUN : LTE_ACT_POWER_CYCLE;
    if (b->fails < LTE_BACKOFF_CAP)
        b->fails++;
    return a;
}

void lte_backoff_ok(lte_backoff_t *b) { b->fails = 0; }
