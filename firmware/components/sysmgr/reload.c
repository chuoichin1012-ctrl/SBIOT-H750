#include "reload.h"

void reload_init(reload_t *r) { *r = (reload_t){0}; }

reload_ev_t reload_step(reload_t *r, bool pressed, uint32_t now_ms)
{
    if (!r->started) {  // held already (the bootloader's restore, a press at power-up): wait for a release
        r->started = true;
        r->armed = !pressed;
        return RELOAD_NONE;
    }
    if (!r->armed) {
        r->armed = !pressed;
        return RELOAD_NONE;
    }
    if (!r->down) {
        if (pressed) {
            r->down = true;
            r->since = now_ms;
            r->hinted = r->hinted_factory = false;
            r->up = 0;
        }
        return RELOAD_NONE;
    }
    if (pressed) {
        r->up = 0;
        uint32_t held = now_ms - r->since;
        if (!r->hinted && held >= RELOAD_RESTART_MS) {
            r->hinted = true;
            return RELOAD_HINT_RESTART;
        }
        if (!r->hinted_factory && held >= RELOAD_FACTORY_MS) {
            r->hinted_factory = true;
            return RELOAD_HINT_FACTORY;
        }
        return RELOAD_NONE;
    }
    if (r->up++ == 0)
        r->upat = now_ms;
    if (r->up < RELOAD_UP_SAMPLES)
        return RELOAD_NONE;
    r->down = false;
    uint32_t held = r->upat - r->since;
    if (held >= RELOAD_FACTORY_MS && held <= RELOAD_MAX_MS)
        return RELOAD_FACTORY;
    return held >= RELOAD_RESTART_MS && held < RELOAD_FACTORY_MS ? RELOAD_RESTART : RELOAD_NONE;
}
