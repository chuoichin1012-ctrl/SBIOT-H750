#include "io_core.h"
#include <stdio.h>
#include <string.h>

static int num(const cJSON *o, const char *k, int dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valueint : dflt;
}

// "HH:MM" or "HH:MM:SS" -> second of the day, -1 when malformed.
static int parse_time(const char *s)
{
    int h, m, sec = 0;
    char extra;
    int n = s ? sscanf(s, "%d:%d:%d%c", &h, &m, &sec, &extra) : 0;
    if (n < 2 || n == 4)  // a character after the seconds: not a time
        return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59)
        return -1;
    return h * 3600 + m * 60 + sec;
}

void io_cfg_parse(const cJSON *io, io_cfg_t *out)
{
    io_cfg_t n = {0};
    char key[32];
    for (int i = 0; i < IO_DO; i++) {
        snprintf(key, sizeof key, "do%dMode", i);
        n.do_mode[i] = num(io, key, IO_DO_FOLLOW);
        snprintf(key, sizeof key, "do%dFlipDelay", i);
        int s = num(io, key, 1);
        n.do_time_ms[i] = (uint32_t)(s < 1 ? 1 : s) * 1000u;
    }
    n.keep = num(io, "keepOutputs", 0) == 1;
    int deb = num(io, "diDebounce", 50);
    n.di_debounce_ms = (uint32_t)(deb < 0 ? 0 : deb);
    n.di_invert = num(io, "diInvert", 0) == 1;
    n.mb_addr = (uint8_t)num(io, "modbusAddress", 100);
    for (int t = 0; t < IO_TIMERS; t++) {
        io_timer_cfg_t *tm = &n.timers[t];
        snprintf(key, sizeof key, "timer%dEnabled", t);
        tm->enable = num(io, key, 0) == 1;
        snprintf(key, sizeof key, "timer%dAction", t);
        tm->restart = num(io, key, 0) == 0;  // 0 = restart the gateway, 1 = set an output
        snprintf(key, sizeof key, "timer%dOutput", t);
        tm->out = num(io, key, 0);
        snprintf(key, sizeof key, "timer%dOutputAction", t);
        tm->action = num(io, key, IO_ACT_OPEN);
        snprintf(key, sizeof key, "timer%dTime", t);
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(io, key);
        tm->sod = parse_time(cJSON_IsString(v) ? v->valuestring : NULL);
        if (tm->sod < 0)
            tm->enable = false;
    }
    *out = n;
}

void io_core_init(io_core_t *c, const io_cfg_t *cfg, uint8_t pins)
{
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    for (int i = 0; i < IO_DI; i++)
        c->di[i] = !((pins >> i) & 1) ^ cfg->di_invert;  // active low
}

// ---- DI -------------------------------------------------------------------------------------------

bool io_core_di(io_core_t *c, int i, bool pin, uint32_t now_ms)
{
    if (i < 0 || i >= IO_DI)
        return false;
    bool raw = !pin ^ c->cfg.di_invert;
    if (raw == c->di[i]) {
        c->di_moving[i] = false;
    } else {
        if (!c->di_moving[i]) {
            c->di_moving[i] = true;
            c->di_since[i] = now_ms;
        }
        // The sample that sees the change counts as one tick, as on the ESP32 (10 ms ticks summed).
        if (now_ms - c->di_since[i] + IO_TICK_MS >= c->cfg.di_debounce_ms) {
            c->di[i] = raw;
            c->di_moving[i] = false;
        }
    }
    return c->di[i];
}

bool io_core_di_get(const io_core_t *c, int i) { return i >= 0 && i < IO_DI && c->di[i]; }

// ---- DO -------------------------------------------------------------------------------------------

static void put(io_core_t *c, int i, bool on)
{
    c->out = (uint8_t)(on ? c->out | (1u << i) : c->out & ~(1u << i));
}

bool io_core_do_get(const io_core_t *c, int i) { return i >= 0 && i < IO_DO && ((c->out >> i) & 1); }

int io_core_do_set(io_core_t *c, int i, bool on, uint32_t now_ms)
{
    if (i < 0 || i >= IO_DO)
        return -1;
    uint32_t period = c->cfg.do_time_ms[i];
    switch (c->cfg.do_mode[i]) {
    case IO_DO_PULSE:
        if (!c->pulsing[i])
            c->rest[i] = io_core_do_get(c, i);
        c->pulsing[i] = on != c->rest[i];  // commanding the rest state ends a pulse; again restarts its time
        c->deadline[i] = now_ms + period;
        put(c, i, on);
        break;
    case IO_DO_BLINK:
        c->blink[i] = on;
        c->deadline[i] = now_ms + period;
        put(c, i, on);
        break;
    default:
        put(c, i, on);
        break;
    }
    return 0;
}

void io_core_tick(io_core_t *c, uint32_t now_ms)
{
    for (int i = 0; i < IO_DO; i++) {
        if ((int32_t)(now_ms - c->deadline[i]) < 0)
            continue;
        if (c->pulsing[i]) {
            put(c, i, c->rest[i]);
            c->pulsing[i] = false;
        } else if (c->blink[i] && c->cfg.do_mode[i] == IO_DO_BLINK) {
            put(c, i, !io_core_do_get(c, i));
            c->deadline[i] += c->cfg.do_time_ms[i];
            if ((int32_t)(now_ms - c->deadline[i]) >= 0)  // a stall: no burst, the period goes on from now
                c->deadline[i] = now_ms + c->cfg.do_time_ms[i];
        }
    }
}

uint8_t io_core_outputs(const io_core_t *c) { return c->out; }

uint8_t io_core_commanded(const io_core_t *c)
{
    uint8_t m = 0;
    for (int i = 0; i < IO_DO; i++) {
        bool on = c->pulsing[i] ? c->rest[i] : c->cfg.do_mode[i] == IO_DO_BLINK ? c->blink[i] : io_core_do_get(c, i);
        m |= (uint8_t)(on << i);
    }
    return m;
}

void io_core_restore(io_core_t *c, uint8_t mask, bool found, uint32_t now_ms)
{
    c->saved = mask;
    c->saved_known = found;
    if (!found)
        return;
    for (int i = 0; i < IO_DO; i++) {
        bool on = (mask >> i) & 1;
        if (c->cfg.do_mode[i] == IO_DO_BLINK)
            io_core_do_set(c, i, on, now_ms);  // blinking resumes
        else
            put(c, i, on);
    }
}

// ---- timers ---------------------------------------------------------------------------------------

static int local_sod(int64_t utc_s, int tz_hours)
{
    int64_t t = (utc_s + (int64_t)tz_hours * 3600) % 86400;
    return (int)(t < 0 ? t + 86400 : t);
}

bool io_core_timers(io_core_t *c, int64_t utc_s, int tz_hours, bool clock_set, uint32_t now_ms)
{
    if (!clock_set) {
        c->last_s = 0;
        return false;
    }
    int64_t last = c->last_s;
    if (last == 0 || utc_s - last > IO_MAX_CATCH_UP_S || last - utc_s > IO_MAX_CATCH_UP_S) {
        c->last_s = utc_s;  // the first check, or the clock was set: start counting from here
        return false;
    }
    if (utc_s <= last)
        return false;  // a small step back: keep the high-water mark, so nothing fires twice
    bool restart = false;
    for (int64_t s = last + 1; s <= utc_s; s++) {
        int sod = local_sod(s, tz_hours);
        for (int t = 0; t < IO_TIMERS; t++) {
            const io_timer_cfg_t *x = &c->cfg.timers[t];
            if (!x->enable || x->sod != sod)
                continue;
            if (x->restart) {
                restart = true;
            } else if (x->out >= 0 && x->out < IO_DO) {
                bool on = x->action == IO_ACT_CLOSE || (x->action == IO_ACT_TOGGLE && !io_core_do_get(c, x->out));
                io_core_do_set(c, x->out, on, now_ms);
            }
        }
    }
    c->last_s = utc_s;
    return restart;
}

// ---- keepOutputs ----------------------------------------------------------------------------------

bool io_core_persist_take(const io_core_t *c, uint8_t *mask)
{
    if (!c->cfg.keep)
        return false;
    *mask = io_core_commanded(c);
    return !c->saved_known || *mask != c->saved;
}

void io_core_persist_done(io_core_t *c, uint8_t mask, bool ok)
{
    if (ok) {
        c->saved = mask;
        c->saved_known = true;
    }
}
