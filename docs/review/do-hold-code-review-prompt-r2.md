Thank you for the do-hold code review (APPROVED, two minors). Both fixed in d29a8fd (`git diff d044b56..d29a8fd -- stm32`):
1. iomgr: LED WORK leaves the Reload window only when reload_step has the debounced release (`!s_button.down`), not on a single up sample.
2. io_check.py: case_reload checks 1 s and 20 s → nothing, 5 s → restart with the settings kept (uptime drops, the hostname mark stays), 12 s → factory settings; the dido case's keepOutputs 0 step now expects the DO held through the restart that applies keepOutputs 0 (that boot still runs with 1), then off after the next restart.
App build ok, check_elf ok, test_reload ok.
Reply with findings, then APPROVED or CHANGES NEEDED.
