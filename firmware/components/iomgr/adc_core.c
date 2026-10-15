#include "adc_core.h"
#include <math.h>
#include <string.h>
#include "crc32.h"

#define VREF 3.0f
#define AMP_GAIN 0.499f  // LM2904 stage: 49.9 kΩ / 100 kΩ
#define SHUNT 0.120f     // kΩ, so that V / kΩ is mA
#define CODE_MAX 65535.0f

void adc_core_init(adc_core_t *a, const adc_cal_t *cal, bool calibrated)
{
    memset(a, 0, sizeof *a);
    a->cal = *cal;
    a->calibrated = calibrated;
}

bool adc_core_half(adc_core_t *a, const uint16_t half[ADC_HALF][ADC_CH], uint32_t seq_start, adc_seq_fn seq_read, void *ctx)
{
    if (a->dead)
        return false;
    uint32_t sum[ADC_CH] = {0};
    for (int i = 0; i < ADC_HALF; i++)
        for (int ch = 0; ch < ADC_CH; ch++)
            sum[ch] += half[i][ch];
    if (seq_read(ctx) != seq_start) {  // the BDMA came round to this half while it was summed
        a->late++;
        return false;
    }
    for (int ch = 0; ch < ADC_CH; ch++)
        a->mean[ch] = (float)sum[ch] / ADC_HALF;
    a->published = true;
    a->windows++;
    return true;
}

void adc_core_fail(adc_core_t *a) { a->published = false; }

void adc_core_dead(adc_core_t *a)
{
    a->dead = true;
    a->published = false;
}

bool adc_core_ok(const adc_core_t *a, int ch) { return ch >= 0 && ch < ADC_CH && a->published && !a->dead; }

float adc_nominal_ma(float code) { return code * VREF / (CODE_MAX * AMP_GAIN * SHUNT); }

int32_t adc_core_ua(const adc_core_t *a, int ch)
{
    if (!adc_core_ok(a, ch))
        return 0;
    float ma = a->cal.gain[ch] * adc_nominal_ma(a->mean[ch]) + a->cal.offset_ma[ch];
    return ma > 0 ? (int32_t)lroundf(ma * 1000.0f) : 0;
}

// ---- factory record -----------------------------------------------------------------------------

static const uint8_t MAGIC[4] = {'S', 'B', 'F', 'R'};

static bool sane(const adc_cal_t *c)
{
    for (int ch = 0; ch < ADC_CH; ch++)  // written so that NaN fails too
        if (!(c->gain[ch] >= 0.8f && c->gain[ch] <= 1.25f) || !(c->offset_ma[ch] >= -1.0f && c->offset_ma[ch] <= 1.0f))
            return false;
    return true;
}

bool frec_parse(const uint8_t *b, size_t len, adc_cal_t *out)
{
    uint32_t crc;
    if (len < FREC_SIZE || memcmp(b, MAGIC, 4) != 0 || b[4] != 1 || b[5] != 0 || b[6] != FREC_SIZE || b[7] != 0)
        return false;
    memcpy(&crc, b + 60, 4);
    if (crc != crc32_update(0, b, 60))
        return false;
    adc_cal_t c;
    memcpy(c.gain, b + 8, sizeof c.gain);
    memcpy(c.offset_ma, b + 16, sizeof c.offset_ma);
    if (!sane(&c))
        return false;
    *out = c;
    return true;
}

void frec_encode(const adc_cal_t *cal, uint8_t out[FREC_SIZE])
{
    memset(out, 0xFF, FREC_SIZE);
    memcpy(out, MAGIC, 4);
    out[4] = 1;
    out[5] = 0;
    out[6] = FREC_SIZE;
    out[7] = 0;
    memcpy(out + 8, cal->gain, sizeof cal->gain);
    memcpy(out + 16, cal->offset_ma, sizeof cal->offset_ma);
    uint32_t crc = crc32_update(0, out, 60);
    memcpy(out + 60, &crc, 4);
}

bool frec_choose(const uint8_t *a, const uint8_t *b, adc_cal_t *out)
{
    if ((a && frec_parse(a, FREC_SIZE, out)) || (b && frec_parse(b, FREC_SIZE, out)))
        return true;
    *out = (adc_cal_t){{1.0f, 1.0f}, {0.0f, 0.0f}};
    return false;
}
