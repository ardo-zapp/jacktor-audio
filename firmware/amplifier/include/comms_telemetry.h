#pragma once
#include <cstdint>
#include "cJSON.h"

void sendFeaturesSnapshot();
void sendRealtimeTelemetry(uint32_t now);
void sendSlowTelemetry(uint32_t now);
void sendAnalyzerSnapshot(const char *evt);
void sendStatsReport();
