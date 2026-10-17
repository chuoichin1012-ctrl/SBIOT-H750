- **Important — `stm32/firmware/test/bench/disc_check.py:110`: cleanup fails when needed.** `board` is never imported. If discovery or restoring the static address fails, the fallback raises `NameError`, leaving the gateway without an address. Reproduced locally with mocked network calls. Import `board`, and move the DHCP save/restart at lines 92–95 inside the `try/finally`, so an exception there also triggers restoration. Add a mocked failure-path test.

No further firmware findings: the hook also admits Ethernet unicast addressed to the gateway’s MAC; replies remain broadcast without ARP. The socket’s netif binding filters out LTE reception and forces Ethernet transmission. Datagram handling still yields under flood.

**CHANGES NEEDED**