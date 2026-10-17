Thank you for r2 (stm32/docs/review/disc-spec-review-r2.md). Revised stm32/docs/specs/2026-10-17-lan-discovery-design.md; please re-review (read-only):

- Important 1: `user` is 1–16 bytes of valid UTF-8 without control characters (the system.username limit, as the Web UI accepts); in both signed strings it is written as lowercase hex of its UTF-8 bytes (`hex(user)`), so no field can contain `|`. Shared vectors include a non-ASCII username (`kỹthuật`) and a UTF-8 password.
- Minor 2: `rng` only when the last nonce generation failed; no current nonce because it was consumed or expired is an `auth` failure (§2.3 step 3–4, §2.4).
- Minor 3: the store test is single-threaded: the host fake lock records lock/read/write/unlock and the test checks store_patch reads and writes within one lock hold, with no unlock between them — the condition that makes a web save from another task wait. (Kept simple instead of a threaded test; tell me if you consider this insufficient.)

Reply with remaining findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
