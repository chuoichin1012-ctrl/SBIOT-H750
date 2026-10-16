// The data services (spec §6.3 step 6, Plans 08a and 08c): the two ports' bridges, Edge when enabled, and
// system.idleRestart. MQTT (Plan 09) joins here.
#pragma once
#include "cJSON.h"

/** After serial_start, before webapi_start. port<n> and system are read once, here. */
void services_start(const cJSON *sys);
/** Data moved to or from the network (a bridge, later MQTT); heartbeats do not count (idleRestart). */
void svc_net_activity(void);
