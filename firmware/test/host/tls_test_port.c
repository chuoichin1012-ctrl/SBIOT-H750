// The mbedTLS port for the host tests (Plan 08b): what tls_port.c does on the board, with a clock the
// test sets and the host's random numbers.
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "mbedtls/platform_time.h"
#include "tls_test_port.h"

int64_t g_test_utc_s;  // 0: the clock is not set

time_t tls_port_time(time_t *t)
{
    if (t)
        *t = (time_t)g_test_utc_s;
    return (time_t)g_test_utc_s;
}

mbedtls_ms_time_t mbedtls_ms_time(void) { return 0; }

int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(out + got, len - got, 0);
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    *olen = len;
    return 0;
}

int test_rng(void *ctx, unsigned char *out, size_t len)
{
    size_t olen;
    return mbedtls_hardware_poll(ctx, out, len, &olen);
}

char *test_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = '\0';
        if (len)
            *len = (size_t)n;
    }
    return buf;
}
