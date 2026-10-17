#include "clock.h"
#include <stdio.h>

static bool leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int month_days(int y, int m)
{
    static const int k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leap(y) ? 29 : k[m - 1];
}

// Exactly `n` digits at s; their value, or -1.
static int digits(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

bool clock_parse(const char *s, civil_t *out)
{
    // "YYYY-MM-DD hh:mm:ss": 19 characters, nothing after.
    if (!s)
        return false;
    for (int i = 0; i < 19; i++)
        if (!s[i])
            return false;
    if (s[19] || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':')
        return false;
    civil_t c = {digits(s, 4), digits(s + 5, 2), digits(s + 8, 2), digits(s + 11, 2), digits(s + 14, 2),
                 digits(s + 17, 2)};
    if (c.year < CLOCK_MIN_YEAR || c.year > CLOCK_MAX_YEAR || c.mon < 1 || c.mon > 12 || c.day < 1 ||
        c.day > month_days(c.year, c.mon) || c.hour < 0 || c.hour > 23 || c.min < 0 || c.min > 59 || c.sec < 0 ||
        c.sec > 59)
        return false;
    *out = c;
    return true;
}

// Days since 1970-01-01 (H. Hinnant, "chrono-Compatible Low-Level Date Algorithms", days_from_civil).
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t clock_to_unix(const civil_t *c)
{
    return days_from_civil(c->year, c->mon, c->day) * 86400 + c->hour * 3600 + c->min * 60 + c->sec;
}

void clock_from_unix(int64_t t, civil_t *out)
{
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int64_t sod = t - days * 86400;
    // civil_from_days, the same paper
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    out->year = (int)(yoe + era * 400 + (m <= 2));
    out->mon = m;
    out->day = d;
    out->hour = (int)(sod / 3600);
    out->min = (int)(sod / 60 % 60);
    out->sec = (int)(sod % 60);
}

int64_t clock_local_to_utc(const civil_t *local, int tz_hours)
{
    return clock_to_unix(local) - (int64_t)tz_hours * 3600;
}

void clock_format(int64_t utc, int tz_hours, char out[20])
{
    civil_t c;
    clock_from_unix(utc + (int64_t)tz_hours * 3600, &c);
    snprintf(out, 20, "%04d-%02d-%02d %02d:%02d:%02d", c.year, c.mon, c.day, c.hour, c.min, c.sec);
}

int64_t clock_device_ms(int64_t real_ms, uint64_t uptime_ms, int tz_hours)
{
    if (real_ms > 0)
        return real_ms;
    return (CLOCK_BOOT_EPOCH - (int64_t)tz_hours * 3600) * 1000 + (int64_t)uptime_ms;
}

void clock_boot_format(uint32_t uptime_s, char out[20]) { clock_format(CLOCK_BOOT_EPOCH + (int64_t)uptime_s, 0, out); }

bool clock_plausible(int64_t utc)
{
    civil_t lo = {CLOCK_MIN_YEAR, 1, 1, 0, 0, 0}, hi = {CLOCK_MAX_YEAR + 1, 1, 1, 0, 0, 0};
    return utc >= clock_to_unix(&lo) && utc < clock_to_unix(&hi);
}
