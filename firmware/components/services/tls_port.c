// mbedTLS's port on the board (spec §7.3, Plan 08b): the clock, the hardware RNG. The functions are what
// the library leaves undefined with tls_config.h (arm-none-eabi-nm -u over all of library/).
#include <stdint.h>
#include "board_rng.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"
#include "now_us.h"
#include "sysmgr.h"
#include "tls_core.h"

// The system clock, 0 while it is not set: never the 2020 display time (sysmgr_display_time).
time_t tls_port_time(time_t *t)
{
    time_t s = (time_t)(sysmgr_utc_ms() / 1000);
    if (t)
        *t = s;
    return s;
}

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)(now_us() / 1000u); }

// The only entropy source (spec §7.1): a failed RNG fails the DRBG seeding, so the session is refused.
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    *olen = 0;
    if (board_rng(out, len) != 0)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

int tls_port_rng(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    return board_rng(out, len) == 0 ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}
