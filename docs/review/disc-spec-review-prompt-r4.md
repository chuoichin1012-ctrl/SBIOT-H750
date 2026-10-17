The user changed two decisions after your r3 approval, and I revised stm32/docs/specs/2026-10-17-lan-discovery-design.md accordingly. Please re-review it (read-only).

User decisions (fixed, not to be reopened):
- No username/password, no authentication: a packet in the agreed format is acted on ("không cần tài khoản/mật khẩu, bức điện đúng message format chúng ta quy định là được"). So HMAC, nonce, lockout, signed acks and the `rng` error are removed. The risk is documented in §5 (anyone on the broadcast domain can change the IP, restart or factory-reset; a later "v":2 could add authentication).
- All packets stay broadcast (also commands to a found gateway), for the cross-subnet case.

Kept from r1–r3: strict packet checks; commands require `mac` (a command without one is ignored, so a stray packet cannot hit every gateway); operation first, then an ack with the real result (ok / bad / store), then the restart; `found` with `eth` (running) and `net` (saved seven fields) and `boot`; store_patch under one lock hold; port 48600 reserved; SO_BINDTODEVICE; the tool's 10 s ack wait, "outcome unknown" without resend, and the 120 s poll until `boot` grows.

Please check only that the revised spec is consistent and complete without authentication, and that nothing left over still refers to the removed machinery. Reply with findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
