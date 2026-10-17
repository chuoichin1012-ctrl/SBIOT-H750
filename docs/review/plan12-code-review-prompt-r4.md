Thank you for r3 (APPROVED at 9492db4). Since then the user asked to evaluate and merge PR #26 (review-fixes) first; I did (bench: serial_check collision and mqtt_check subscribe flood ok on the board), and merged main into `discovery`: merge commit f9ebf57 (`git diff 377d13f f9ebf57 -- stm32` shows what main brought in; `git show f9ebf57` the merge). No textual conflict.

Please check the interaction only (read-only):
- store: PR #26 added store_port_progress() per erased sector (fal_nor) and its host stub in test_store.c; Plan 12 added store_patch and the lock trace in test_store.c. disc.c's `factory` path calls store_factory_reset from the `disc` task (30 s supervisor timeout): it now beats per sector through wdog_hb_beat_self.
- web: PR #26 changed form.tsx, validate.ts and the Edge pages; Plan 12 changed rules.ts and Port.tsx.
- Verified on the merged tree: test_store, test_store_small, test_disc_core ok; npm test:unit 25/25, tsc ok; boot and app build, check_elf ok; the merged app installed by web OTA and disc_check.py 19/19 ok on the board (incl. the 70 s flood, 4.6 M datagrams, and the UART1-on-48600 case).

Reply with findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
