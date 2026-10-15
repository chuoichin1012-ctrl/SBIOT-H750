// Calendar arithmetic for the gateway clock (spec §10.4). No mktime, no TZ: local time is UTC plus
// the whole-hour `timezone` of the system config (-12 to +12). Pure C, host-tested (test_clock).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CLOCK_MIN_YEAR 2024
#define CLOCK_MAX_YEAR 2099

typedef struct {
    int year, mon, day;  // mon 1-12, day 1-31
    int hour, min, sec;
} civil_t;

/** Strict "YYYY-MM-DD hh:mm:ss": real dates only, years CLOCK_MIN_YEAR to CLOCK_MAX_YEAR. */
bool clock_parse(const char *s, civil_t *out);
/** Seconds since 1970-01-01 00:00:00 for this date and time, read as UTC. */
int64_t clock_to_unix(const civil_t *c);
void clock_from_unix(int64_t t, civil_t *out);
/** A local date and time in zone tz_hours, as UTC seconds. */
int64_t clock_local_to_utc(const civil_t *local, int tz_hours);
/** UTC seconds as local "YYYY-MM-DD hh:mm:ss" in zone tz_hours (out holds 20 bytes). */
void clock_format(int64_t utc, int tz_hours, char out[20]);
/** True for a time inside CLOCK_MIN_YEAR..CLOCK_MAX_YEAR (UTC): what SNTP may set. */
bool clock_plausible(int64_t utc);
