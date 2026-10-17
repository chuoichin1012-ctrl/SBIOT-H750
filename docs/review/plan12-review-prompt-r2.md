Thank you for r1 (stm32/docs/review/plan12-review-r1.md). I revised stm32/docs/plans/2026-10-17-12-lan-discovery.md; please re-review (read-only):

1. disc_start binds the socket synchronously in init (try_open) before services_start; the task retries only if the Ethernet was not up. Bench case: UART1 set (through the API, around the UI) as a UDP server on 48600, restart: the board is still found and UART1 reports its bind error; UART1 restored after.
2. disc_parse first requires the whole datagram to be one strict JSON value (jsonscan js_parse_root: nothing after it, no `01`), no raw NUL, no `\u0000` escape; new shared vectors: garbage after the object, two objects, a leading-zero number, an escaped NUL in mac and in op.
3. The task does vTaskDelay(1) after every datagram, ignored or empty ones too. Bench: a 20 s broadcast flood of junk to 48600 from Windows, then the board answers with the same boot.
4. NetSettings.Validate: gateway 0.0.0.0 = none (isolated LAN), relation checks only when nonzero; DNS syntax only. Tests changed accordingly.
5. WinForms handlers catch SocketException/ObjectDisposedException/InvalidOperationException, restore controls in finally; a failure after the send says "outcome unknown", nothing resent; the browser start is caught too.
6. IncludeNativeLibrariesForSelfExtract=true; the publish checks no native dll beside the exe; the exe is copied alone into a clean folder for the user's run.
7. Lock trace: cJSON allocation hook and the flash program hook record the lowest lock depth and the release count at the first allocation and at the last program; store_patch now takes the lock before its argument checks, so every allocation is under it. The OOM sweep checks the whole saved document on STORE_OK.
8. Bench: requires priority 0 at the start, captures /api/config/network before and compares it whole after (and port1 likewise).
9. `set -o pipefail` on every piped command; the publish clears `out` first.

Reply with remaining findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
