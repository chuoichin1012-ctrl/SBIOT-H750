# FlashDB 2.2.0

Key-value and time-series databases on the SPI4 flash (spec §5.2, §9.1), with its FAL layer.

- Source: https://codeload.github.com/armink/FlashDB/tar.gz/refs/tags/2.2.0,
  SHA-256 `7dab46c1ce8203f2ebd267e1ef0c0627cd08a4b0b7722569436994c31ae9039e`.
- Files kept, unchanged except one patch: `src/{fdb,fdb_kvdb,fdb_tsdb,fdb_utils}.c`, `inc/{fdb_def,fdb_low_lvl,flashdb}.h`,
  `port/fal/src/{fal,fal_flash,fal_partition}.c`, `port/fal/inc/*`. They equal the copy the ESP32
  firmware uses (`firmware/components/flashdb/FlashDB`). Left out: `fdb_file.c` (file mode),
  `fal_rtt.c` (RT-Thread), templates, docs, tests.
- **Patch** `../patches/flashdb-2.2.0-recovery-rescan.patch` (in `src/fdb_kvdb.c`, marked
  "SBIOT patch"): on load, after a KV left half-written (`PRE_WRITE`) is marked bad, the recovery
  scan stopped, so the old value of that KV (`PRE_DELETE`, further on in the scan) was not
  recovered in that load and read as missing. Found by the power-cut matrix of
  `test/host/test_store.c` (a manifest rewrite cut in its name or value, the database full enough
  to collect garbage); upstream master still has the stop. The patch scans again until no
  half-written KV is left.
- Our configuration (`fdb_cfg.h`, `fal_cfg.h`) and the flash device live in `components/store/port`.
- Licence: Apache-2.0 (`LICENSE`, `port/fal/LICENSE`).
