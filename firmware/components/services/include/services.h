// The data services (spec §6.3 step 6, Plan 08a): the two ports' bridges and system.idleRestart. MQTT
// (Plan 09) and Edge (Plan 08c) join here.
#pragma once
#include "cJSON.h"

/** After serial_start, before webapi_start. port<n> and system are read once, here. */
void services_start(const cJSON *sys);
/** Data moved to or from the network (a bridge, later MQTT); heartbeats do not count (idleRestart). */
void svc_net_activity(void);
