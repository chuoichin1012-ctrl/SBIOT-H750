- **P2 — `stm32/firmware/components/webapi/console.c:209–221`: split writes still lose their remaining chunks.** For a 2000-byte write, the first 1460-byte chunk produces 4383 bytes of text plus its WebSocket header, exceeding `WEB_SEND_HIGH` (4096). `drain()` immediately dequeues and drops the remaining 540-byte chunk. Mongoose only queues these sends; no socket flush occurs between iterations. This happens even with an otherwise empty queue and a fast client.

  Retain pending chunks across polls until the send queue has room. Add a drain-level test proving a 2000/4000-byte write reaches a reading client completely; the splitter test alone cannot catch this.

The TX-before-write ordering fix is addressed. No board tests were run.

**CHANGES NEEDED**