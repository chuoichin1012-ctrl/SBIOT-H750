// Host tests for the gateway clock (spec §10.4): make -C stm32/firmware/test/host
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "clock.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void test_parse(void)
{
    civil_t c;
    CHECK(clock_parse("2026-10-15 13:45:09", &c) && c.year == 2026 && c.mon == 10 && c.day == 15 && c.hour == 13 &&
              c.min == 45 && c.sec == 9,
          "a good time");
    CHECK(clock_parse("2024-02-29 00:00:00", &c), "Feb 29 of a leap year");
    static const char *bad[] = {
        "2025-02-29 00:00:00", "2026-04-31 00:00:00", "2026-13-01 00:00:00", "2026-00-10 00:00:00",
        "2026-10-00 00:00:00", "2026-10-15 24:00:00", "2026-10-15 23:60:00", "2026-10-15 23:59:60",
        "2023-12-31 23:59:59", "2100-01-01 00:00:00", "2026-10-15 13:45:09x", "2026-10-15 13:45:9",
        "2026-10-15T13:45:09", " 2026-10-15 13:45:09", "2026-1-15 13:45:09", "", "+026-10-15 13:45:09",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(!clock_parse(bad[i], &c), "refused: \"%s\"", bad[i]);
    CHECK(!clock_parse(NULL, &c), "NULL refused");
}

static void test_unix(void)
{
    // Against the C library's timegm over the whole range, one day and a few seconds at a time.
    int bad = 0;
    for (int64_t t = 1704067200; t < 4102444800; t += 86400 + 3607) {  // 2024-01-01 .. 2100-01-01
        civil_t c;
        clock_from_unix(t, &c);
        time_t tt = (time_t)t;
        struct tm g;
        gmtime_r(&tt, &g);
        if (c.year != g.tm_year + 1900 || c.mon != g.tm_mon + 1 || c.day != g.tm_mday || c.hour != g.tm_hour ||
            c.min != g.tm_min || c.sec != g.tm_sec || clock_to_unix(&c) != t)
            bad++;
    }
    CHECK(bad == 0, "civil <-> unix agrees with gmtime over 2024-2099 (%d off)", bad);
    civil_t e = {1970, 1, 1, 0, 0, 0};
    CHECK(clock_to_unix(&e) == 0, "the epoch");
}

static void test_zone(void)
{
    civil_t c;
    clock_parse("2026-10-15 07:00:00", &c);
    int64_t u = clock_local_to_utc(&c, 7);
    char out[20];
    clock_format(u, 0, out);
    CHECK(strcmp(out, "2026-10-15 00:00:00") == 0, "UTC+7 07:00 is 00:00 UTC (%s)", out);
    clock_format(u, 7, out);
    CHECK(strcmp(out, "2026-10-15 07:00:00") == 0, "and back (%s)", out);
    clock_format(u, -12, out);
    CHECK(strcmp(out, "2026-10-14 12:00:00") == 0, "UTC-12 (%s)", out);
    clock_format(u, 12, out);
    CHECK(strcmp(out, "2026-10-15 12:00:00") == 0, "UTC+12 (%s)", out);
    clock_parse("2024-01-01 03:00:00", &c);
    clock_format(clock_local_to_utc(&c, 7), 0, out);
    CHECK(strcmp(out, "2023-12-31 20:00:00") == 0, "across a year (%s)", out);
}

static void test_plausible(void)
{
    CHECK(!clock_plausible(0), "1970 is not a set clock");
    CHECK(clock_plausible(1760500000), "2025 is");
    CHECK(!clock_plausible(4102444800), "2100 is not");
}

// Plan 08b T1: while the clock is not set, the display reads 2020-01-01 00:00:00 plus the uptime.
static void test_boot_display(void)
{
    char out[20];
    clock_boot_format(0, out);
    CHECK(strcmp(out, "2020-01-01 00:00:00") == 0, "at boot (%s)", out);
    clock_boot_format(86399 + 60, out);
    CHECK(strcmp(out, "2020-01-02 00:00:59") == 0, "a day later (%s)", out);
    clock_boot_format(49u * 86400 + 17 * 3600, out);
    CHECK(strcmp(out, "2020-02-19 17:00:00") == 0, "past the 32-bit ms wrap (%s)", out);
    civil_t c;
    CHECK(!clock_parse("2020-01-01 00:00:00", &c), "the display time cannot be set back in (CLOCK_MIN_YEAR)");
}

int main(void)
{
    test_boot_display();
    test_parse();
    test_unix();
    test_zone();
    test_plausible();
    printf("test_clock: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
