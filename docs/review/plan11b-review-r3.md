**Critical:** None found.

**Important**

1. **Handshake timestamps are not comparable across peer processes.**  
   **Task 4 Step 2(3); `win_peer.py:34–41`.** Each peer’s `t` is relative to its own process start. Intersecting those intervals can falsely report overlapping handshakes.  
   **Fix:** add a common-clock timestamp to accept/handshake events on Windows, or run all three listeners in one process. HTTPS also needs the proposed accept/handshake events.

2. **Outbox-full detection references fields that do not exist.**  
   **Task 4 Step 2(1–2); `components/webapi/api.c:201–205`.** `/api/status.mqtt` exposes only `enabled` and `state`, not occupancy or drops. Also, the plan still leaves restart persistence as a question: `bufferOffline` does **not** preserve the RAM outbox across reboot (`mqttgw.c` initializes it with `ob_init`).  
   **Fix:** configure all endpoints and restart **before** filling. Leave the intended MQTT endpoint stopped, fill until a new existing `"outbox full"` log proves saturation, then start the TLS peer at that same endpoint without restarting the gateway.

**Minor**

- **Task 1, `pick()`:** rejecting every address ending `.255` assumes a `/24`. For example, `192.168.0.255/23` is a valid host address. Either explicitly constrain this bench helper to the known `/24`, or determine directed broadcast using the actual mask.

The revised boot-count source, largest-block field, mutual-TLS handling and watchdog criteria address the previous findings.

**CHANGES NEEDED.**