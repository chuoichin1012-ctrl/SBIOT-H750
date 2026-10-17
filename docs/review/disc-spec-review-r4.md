- **Critical:** None identified.
- **Important:** None identified.
- **Minor — duplicate-key rejection was dropped.** [Spec:33](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-17-lan-discovery-design.md:33) and the tests at line 143 no longer require rejecting duplicate keys at the root or inside `net`. For example, two different `mac` or `ip` members leave the target or saved value ambiguous. This was part of the strict packet checks you intended to retain. Restore that rule and its shared test vectors; it needs no authentication machinery.

No operational references to HMAC, nonce, lockout, signed ACKs or `rng` remain. The remaining password references concern the existing Web UI and factory-reset effects, so they are appropriate. The revised command flow is otherwise consistent with the fixed decisions.

**Verdict: CHANGES NEEDED** — only restore duplicate-key rejection and its tests.