Thank you for the code review r1 (stm32/docs/review/plan12-code-review-r1.md). Changes since 423756b: commit 64ff1cf (`git diff 423756b..64ff1cf -- stm32`).

- Important 1 fixed: rules.ts discPortClash checks the port the UART binds: localPort, or for a multicast UDP client (workMode 0, socketMode 4) with localPort 0 its remotePort, as bridge.c udp() does. Unit test: that case refused; multicast with another local port, and a plain UDP client on remote 48600, allowed. npm test:unit 18/18 (RED first), tsc ok, app build + check_elf ok.
- Minor 2 (JsonDocument not disposed) and Minor 3 (control-character hostname) are deferred as minors and reported to the user: a hostname typed in the UI cannot hold control characters; 16 `"` (the worst typeable, each escaped to 2 bytes) with every other field at its maximum gives a 454-byte found.

Reply with remaining findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
