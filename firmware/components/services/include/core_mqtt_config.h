// coreMQTT 2.3.1's settings for the SBIOT-H750 (spec §8, Plan 09 ruling 2). coreMQTT's own loops call the
// transport again without a pause when it returns 0: mqtt_transport waits before that, and these bound the loops.
#pragma once

#define MQTT_SEND_TIMEOUT_MS 10000U          // a whole packet out (sendBuffer)
#define MQTT_RECV_POLLING_TIMEOUT_MS 50U     // the rest of a packet once its first bytes came (recvExact)
#define MQTT_PINGRESP_TIMEOUT_MS 10000U
#define MQTT_STATE_ARRAY_MAX_COUNT 16U       // in flight each way (MQTT_InitStatefulQoS)

// Errors, warnings and infos to the log (Debug off). coreMQTT writes LogError( ( "fmt", args ) ).
void coremqtt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LogError(message) coremqtt_log message
#define LogWarn(message) coremqtt_log message
#define LogInfo(message)
#define LogDebug(message)
