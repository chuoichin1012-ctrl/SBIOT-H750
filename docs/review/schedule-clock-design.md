**APPROVED**, with these behavioral clarifications in the spec and tests:

- This supersedes the proposed NTP-based UI restriction: Scheduled remains available with NTP off.
- Existing `edge_schedule_due` checks the **whole matching minute**. Thus hourly/15-minute schedules can fire during the first minute after boot, rather than waiting a full interval.
- Switching to real time can produce another report immediately if that time matches the schedule. The synthetic and real epoch minutes are different.
- Narrow “the same minute never fires twice” to **repeated checks of the last-fired minute**. The current equality check does not prevent repeats after a backward clock correction.

Add tests for the initial minute and a switch into a matching real-time minute. No additional recovery or scheduling mechanism is needed.