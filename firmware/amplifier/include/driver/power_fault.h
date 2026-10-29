#pragma once
#include <cstdint>

void powerFaultInit();
void powerFaultTick(uint32_t now, bool isRelayOn, bool inSoftstart, float voltage, float cutV, float recV);

bool powerFaultSmpsIsCutActive();
bool powerFaultSmpsIsTripLatched();
void powerFaultSmpsReset();
void powerFaultSetSmpsCutActive(bool active);
