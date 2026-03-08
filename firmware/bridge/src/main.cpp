#include <Arduino.h>
#include "config.h"
#include "comms.h"
#include "display.h"

void setup() {
  commsInit();
  displayInit();
  // Karena OTG dihapus, dan Native USB diinisialisasi secara background
  // pada framework Arduino (build_flags ARDUINO_USB_CDC_ON_BOOT=1),
  // setup langsung selesai.
}

void loop() {
  uint32_t now = millis();
  commsTick(now);
  displayTick(now);
}
