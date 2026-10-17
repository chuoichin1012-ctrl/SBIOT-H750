// Bench (Plan 07b), from the UART7 console (app_main.c):
//   a                        the AI: raw window means, µA, ok, the counters and the ADC3 registers
//   A <g1> <o1> <g2> <o2>    writes the factory record, both copies (gain, offset in mA, per channel);
//                            applies after a restart. The production loader is Plan 11.
//   J                        an ADC fault (the BDMA channel off: the next conversion overruns)
//   C                        both factory record copies read back from SPI4: parsed or not, and their coefficients
//                            in millionths (tools/factory_cal.py, Plan 11a task 4)
#include "bench_io.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "adc_core.h"
#include "adc_drv.h"
#include "blog.h"
#include "flash_spi4.h"
#include "iomgr.h"

static int write_copy(nor_t *n, uint32_t at, const uint8_t *rec)
{
    uint8_t back[FREC_SIZE];
    if (n->erase_sector(n, at) || n->program(n, at, rec, FREC_SIZE) || n->read(n, at, back, sizeof back))
        return -1;
    adc_cal_t c;
    return frec_parse(back, sizeof back, &c) ? 0 : -1;
}

void bench_io_cmd(char cmd, const char *line)
{
    if (cmd == 'a') {
        io_status_t s;
        iomgr_status(&s);
        blog("app: a mean %ld.%02ld %ld.%02ld ua %ld %ld ok %d %d cal %d windows %lu late %lu irqs %lu errors %lu "
             "recoveries %lu cr %08lx cfgr %08lx cfgr2 %08lx smpr1 %08lx\r\n",
             (long)s.mean[0], (long)((s.mean[0] - (long)s.mean[0]) * 100), (long)s.mean[1],
             (long)((s.mean[1] - (long)s.mean[1]) * 100), (long)iomgr_ai_ua(0), (long)iomgr_ai_ua(1), iomgr_ai_ok(0),
             iomgr_ai_ok(1), s.ai_calibrated, (unsigned long)s.adc_windows, (unsigned long)s.adc_late,
             (unsigned long)s.adc_irqs, (unsigned long)s.adc_errors, (unsigned long)s.adc_recoveries,
             (unsigned long)s.cr, (unsigned long)s.cfgr, (unsigned long)s.cfgr2, (unsigned long)s.smpr1);
    } else if (cmd == 'A') {
        adc_cal_t c;
        char *p = (char *)line;
        c.gain[0] = strtof(p, &p);
        c.offset_ma[0] = strtof(p, &p);
        c.gain[1] = strtof(p, &p);
        c.offset_ma[1] = strtof(p, &p);
        uint8_t rec[FREC_SIZE];
        frec_encode(&c, rec);
        adc_cal_t check;
        if (!frec_parse(rec, sizeof rec, &check)) {
            blog("app: A refused: coefficients out of range\r\n");
            return;
        }
        nor_t *n = flash_spi4();
        int ra = write_copy(n, FREC_A, rec), rb = write_copy(n, FREC_B, rec);
        blog("app: A record written: copy A %s, copy B %s (applies after a restart)\r\n", ra ? "FAILED" : "ok",
             rb ? "FAILED" : "ok");
    } else if (cmd == 'C') {
        nor_t *n = flash_spi4();
        const uint32_t at[2] = {FREC_A, FREC_B};
        char part[2][80];
        for (int k = 0; k < 2; k++) {
            uint8_t b[FREC_SIZE];
            adc_cal_t c = {{0, 0}, {0, 0}};
            bool ok = n && !n->read(n, at[k], b, sizeof b) && frec_parse(b, sizeof b, &c);
            snprintf(part[k], sizeof part[k], "%s %ld %ld %ld %ld", ok ? "ok" : "bad", lroundf(c.gain[0] * 1e6f),
                     lroundf(c.offset_ma[0] * 1e6f), lroundf(c.gain[1] * 1e6f), lroundf(c.offset_ma[1] * 1e6f));
        }
        blog("app: C A %s B %s\r\n", part[0], part[1]);
    } else if (cmd == 'D') {
        extern volatile bool g_io_trace;
        g_io_trace = !g_io_trace;
        blog("app: D output trace %s\r\n", g_io_trace ? "on" : "off");
    } else if (cmd == 'J') {
        adc_drv_inject_fault();
        blog("app: J ADC fault injected\r\n");
    }
}
