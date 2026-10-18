#pragma once
#include <cstdint>

// High-level app lifecycle entry points
void appInit();
void appTick();

// Jalankan factory reset terkelola (buzzer + log + reboot)
void appPerformFactoryReset(const char* subtitle, const char* src);
