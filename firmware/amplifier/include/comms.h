#pragma once
#include <cstdint>
#include <cstddef>

// Initialize UART2 communication link to main panel
void commsInit();

// Called regularly in main loop (sqwTick=true when receiving 1 Hz RTC pulse)
void commsTick(uint32_t now, bool sqwTick);

// Request immediate telemetry transmission (e.g., after critical state change)
void commsForceTelemetry();

// OTA guard notification (informs panel that amplifier is undergoing OTA)
void commsSetOtaReady(bool ready);

// Send short log message (optional)
void commsLog(const char* level, const char* msg);

// Specific log for factory reset (includes trigger source)
void commsLogFactoryReset(const char* src);
