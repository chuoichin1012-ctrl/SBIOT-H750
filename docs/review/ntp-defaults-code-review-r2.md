- **Minor — [firmware-design.md:965](/home/linh/projects/SBIOT-Gateway/stm32/docs/specs/2026-10-14-stm32-firmware-design.md:965):** The spec still promises an immediate request at boot and after every route change. Update it to match: start once when the first route is available; subsequent changes retain SNTP’s retry/update schedule.

No remaining P1/P2 findings. Both startup orders are covered under the core lock, and the start-once guard removes the duplicate-DNS-callback issue.

**APPROVED**