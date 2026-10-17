- **P2 — `stm32/firmware/components/services/edge_core.c:294–303`: an initially true condition can remain inactive forever.** `fill_links()` initializes `last_fire` to zero. With `minInterval=10000`, a first reading of 60 at uptime 2 s for an ON ≥ 50 event records `last_state=true` but suppresses the action. If the reading stays above 50, it never triggers—even after 10 s. Apply the minimum gap only after an actual previous trigger; add a first-sample-before-gap test.

- **P2 — `stm32/web/src/lib/edge.ts:402–409`: normalization changes the firmware’s effective configuration.** The firmware’s `cfg_edge_load` first applies `ITEM_SLAVE`/`ITEM_NODE` defaults: UART1, poll 1000, port 502, FC3, timeout 200, range 1. `normalizeSlaves` substitutes zero instead. A valid sparse saved model consequently shows invalid fields and cannot be saved unchanged. Accepted boolean values such as `onChange:1` also become false. Mirror the actual loader defaults and conversions; the new test asserting FC0 currently reinforces the error.

- **Minor — `stm32/web/src/lib/rules.ts:108–114`: wildcard matching misses the `$` exception.** Request `#` with reply `$device/reply` is rejected as a loop, although the broker and firmware’s `mqttutil.c:10` exclude `$` topics from leading wildcards. Add the same guard and a test.

Read-only checks reproduced both UI mismatches; no board tests were run.

**CHANGES NEEDED**