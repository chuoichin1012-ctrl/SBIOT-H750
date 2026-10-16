// The data services (spec §6.3 step 6, Plans 08a, 08c and 09): the two ports' bridges, MQTT and Edge when
// enabled, and system.idleRestart.
#pragma once
#include "cJSON.h"

/** After serial_start, before webapi_start. port<n> and system are read once, here. */
void services_start(const cJSON *sys);
/** Data moved to or from the network (a bridge, MQTT); heartbeats do not count (idleRestart). */
void svc_net_activity(void);
