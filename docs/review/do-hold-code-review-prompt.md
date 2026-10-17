Code review (read-only) of branch do-hold: `git diff main..d044b56 -- stm32` (commits 48a6e1f and d044b56), after your design review (stm32/docs/review/do-hold-design.md, CHANGES NEEDED: all four points taken).

1. Restart Hold (48a6e1f):
   - dohold.c/h (pure, test_board): do_hold_t (magic "DOHD", keep, latched, commanded, CRC) at DO_HOLD_ADDR 0x3800FBE0 (app.ld `.dohold`, NOLOAD; check_elf gates the address, size, no overlap, outside the bootloader); do_hold_keep(por, bor, rec).
   - PD14 (LE) and PD15 (OE) removed from both .ioc (cubegen.sh regenerated; mkioc.py and check_pins.py follow), so MX_GPIO_Init never touches them; board_latch_init(hold) sets them up by hand: levels first (BSRR), then output mode; hold: LE=OE=0; else OE=1, D=0, LE pulse, OE=0.
   - Bootloader boot_early: first thing, decide from RCC_RSR + the record, board_latch_init(hold), invalidate the record when not holding, then MX_GPIO_Init. boot_main no longer latches.
   - App app_main: the same decision before report_boot clears RSR; iomgr_start(..., held): on hold s_latched = record.latched and io_core_restore(record.commanded), else from 0; latch() invalidates+cleans the record before changing the latch, publishes after; also publishes when only `commanded` changes, and once at start.
   - io_core: io_core_restore(c, mask, now) always at boot (follow value, pulse rest, flip phase); Timer flip flips by itself from boot; a command in flip sets the output and the flipping goes on; io_core_commanded = the flip's phase; flash persistence (io_core_persist_*, store_state "do") removed.
   - UI help (IO page), spec §6.1 (DO modes), §11.2 step 2 (hold vs safe, the R103 limitation), §9.
2. Reload (d044b56): reload_step: release at 3 s..<10 s → RELOAD_RESTART (iomgr: sysmgr_restart_later(500)); 10–15 s → RELOAD_FACTORY; hints at 3 s and 10 s; wdog_set_button(stage) shows the window on LED WORK (0.2 s from 3 s, 0.5 s from 10 s, normal once released; an OTA or halt shows first). Spec §11.4.

Verified: host test_board, test_io_core, test_io_mb, test_reload ok (RED first for dohold, io_core, reload); boot and app build with no warning; check_elf and check_pins ok; npm test:unit 25/25, tsc ok. On the board (new bootloader over SWD, app over web OTA): "io: outputs held through the restart (latch …)" after an NRST; Timer flip DO2 2 s flips from boot (sampled 2 s on/2 s off). The user then tested on the board and reports everything works as expected: Restart Hold over web restart and the Reload restart (relay not released), power cycle → all off, Timer flip from start-up, the Reload windows and LED. Not measured by us with a scope (the user checked the relays).

Reply with findings ranked (Critical / Important / Minor) with file:line, then a verdict: APPROVED or CHANGES NEEDED.
