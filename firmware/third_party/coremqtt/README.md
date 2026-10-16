# coreMQTT 2.3.1

The MQTT client of the gateway (spec §8, Plan 09), driven by the `mqtt` task only (`components/services/mqttgw.c`).

- **Source:** https://github.com/FreeRTOS/coreMQTT/releases/download/v2.3.1/coreMQTT-v2.3.1.zip (released 2024-07-12, the
  last 2.x; v5 is MQTT 5), with SHA-256 `4bf0d8de9fbed4b35c5f149f7009b06946834ea3d8384b1d39e1c392f8316563`, taken on download
  (the release carries no digest).
- **Files kept:** `source/` (the `*.readme` notes removed) and `LICENSE` (MIT).
- **Settings:** `components/services/include/core_mqtt_config.h`.
- **Patched in place**, the change recorded in `third_party/patches/coremqtt-2.3.1-sbiot.patch`:
  - **MQTT 3.1** (`MQTTConnectInfo_t.mqtt31`, false = 3.1.1): CONNECT with "MQIsdp" at level 3 (Remaining Length 2 bytes
    longer); the 3.1 CONNACK's reserved first byte is not checked, and the session is kept exactly when `cleanSession` is
    false. 3.1.1 is unchanged. Tested byte for byte by `test/host/test_mqtt_wire.c`.
  - **Incoming messages larger than the network buffer** (Plan 09 ruling 11, in T3): their QoS 1/2 exchange is completed
    although the payload is dropped.
