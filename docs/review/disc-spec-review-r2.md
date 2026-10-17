## Critical

None identified.

## Important

1. **Finder rejects credentials that the Web UI accepts.**  
   [Discovery spec:40](stm32/docs/specs/2026-10-17-lan-discovery-design.md#L40)

   The new ASCII-only username rule rejects, for example, `kỹthuật`, even though the existing [System form](stm32/web/src/pages/System.tsx#L27) accepts it within the 16-byte limit and [web authentication](stm32/web/src/lib/auth.ts#L24) encodes credentials as UTF-8. A technician with these credentials cannot use Finder to repair an inaccessible IP configuration.

   **Fix:** accept the same bounded UTF-8 usernames as the web interface. Specify an unambiguous encoding for the username in the signed string—for example, hex-encoded UTF-8 bytes—and add C/C# vectors. Also correct line 42: printable ASCII includes `|`.

## Minor

2. **Consumed nonce and RNG failure need distinct outcomes.**  
   [Spec:85](stm32/docs/specs/2026-10-17-lan-discovery-design.md#L85), [114](stm32/docs/specs/2026-10-17-lan-discovery-design.md#L114)

   Authentication destroys the current nonce. The next command therefore encounters “no valid nonce,” which step 3 maps to `rng`; §2.4 and the tests instead require `auth` for the second command using that nonce.

   **Fix:** explicitly return `rng` only after nonce generation failed; consumed, mismatched or expired nonces return `auth`. This requires distinguishing the reason, not adding another recovery mechanism.

3. **The proposed interleaving test must exercise another caller.**  
   [Spec:184](stm32/docs/specs/2026-10-17-lan-discovery-design.md#L184)

   Calling `store_set_text` directly from the read/write hook would be reentrant: the board’s store mutex is recursive, while the current [host lock](stm32/firmware/test/host/test_store.c#L62) merely counts nesting. Neither would block that call, so this would not test exclusion between web and Finder tasks.

   **Fix:** have the hook signal a competing caller and verify that its write cannot complete until `store_patch` releases the lock. Check both operation orders.

The revised ACK signing, operation-before-ACK sequencing, saved/running network separation and Ethernet-bound socket approach address the substantive r1 concerns. No raw API or additional protocol machinery is needed.

**Verdict: CHANGES NEEDED.**