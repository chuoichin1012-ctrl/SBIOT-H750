# FlashDB 2.2.0

Key-value and time-series databases on the SPI4 flash (spec §5.2, §9.1), with its FAL layer.

- Source: https://codeload.github.com/armink/FlashDB/tar.gz/refs/tags/2.2.0,
  SHA-256 `7dab46c1ce8203f2ebd267e1ef0c0627cd08a4b0b7722569436994c31ae9039e`.
- Files kept, unchanged except one patch file: `src/{fdb,fdb_kvdb,fdb_tsdb,fdb_utils}.c`, `inc/{fdb_def,fdb_low_lvl,flashdb}.h`,
  `port/fal/src/{fal,fal_flash,fal_partition}.c`, `port/fal/inc/*`. They equal the copy the ESP32
  firmware uses (`firmware/components/flashdb/FlashDB`). Left out: `fdb_file.c` (file mode),
  `fal_rtt.c` (RT-Thread), templates, docs, tests.
- **Patch** `../patches/flashdb-2.2.0-sbiot.patch` (all in `src/fdb_kvdb.c`, each marked
  "SBIOT patch"). Upstream master has all three faults. Each was found by a matrix of
  `test/host/test_store.c`:
  1. **Recovery stopped early.** On load, after a KV left half-written (`PRE_WRITE`) is marked bad, the recovery scan stopped. The old value of that KV (`PRE_DELETE`, further on) was not recovered in that load and read as missing. Found by the power-cut matrix: a manifest rewrite cut in its name or value, with garbage collection. The scan now runs again, only after a status write that worked, at most 16 times.
  2. **A failed move counted as done.** `move_kv` ignored a failed copy, and a failed final status write, and deleted the source anyway; `do_gc` then formatted the sector. One failing write during garbage collection lost a live KV. Now a failed move keeps its source (`PRE_DELETE`, recovered at the next load), and `do_gc` leaves that sector for the next collection.
  3. **The target sector's status went unchecked.** `move_kv` ignored a failed status write of its target sector. The KV was moved into a sector still marked empty, which the next load skips. Now that failure keeps the source.

  Faults 2 and 3 were found by the single-write-failure matrix on a FlashDB churn that collects garbage.
- Our configuration (`fdb_cfg.h`, `fal_cfg.h`) and the flash device live in `components/store/port`.
- Licence: Apache-2.0 (`LICENSE`, `port/fal/LICENSE`).
