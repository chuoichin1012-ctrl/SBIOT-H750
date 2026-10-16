# Mbed TLS 3.6.7 (LTS)

TLS 1.2 client for the bridges (TCP client, HTTPS) and, from Plan 09, MQTT (spec §7.3, Plan 08b).

- **Source:** https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 (released 2026-07-07, the 3.6 LTS branch), with SHA-256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` (the release's own digest).
- **Files kept, unchanged:** `library/*.c` and `library/*.h` (the build files removed), `include/`, `LICENSE`. No patches.
- **Licence:** Apache-2.0 OR GPL-2.0-or-later, used under Apache-2.0.
- **Settings:** `components/services/include/tls_config.h`, passed as `MBEDTLS_CONFIG_FILE="tls_config.h"`.
  The name must differ from `mbedtls_config.h`: `build_info.h` would find the library's default config
  in its own directory first.
- **Port functions** (`components/services/tls_port.c`, `tls_core.c`): `mbedtls_hardware_poll` (the
  hardware RNG), `tls_port_time` (the system clock, 0 while unset), `mbedtls_ms_time`,
  `mbedtls_platform_gmtime_r` (on `clock_from_unix`).
