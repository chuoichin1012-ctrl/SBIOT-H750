// FinderTool's discovery on the gateway (Plan 12; stm32/docs/specs/2026-10-17-lan-discovery-design.md).
#pragma once
#include "cJSON.h"
/** Binds UDP 48600 on the Ethernet netif (now, so before the bridges) and starts the `disc` task. Reads
 *  system.hostname and system.webPort once. Call before services_start. */
void disc_start(const cJSON *sys);
