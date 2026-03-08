#include <Arduino.h>
#include "config.h"
#include "comms.h"
#include "display.h"
#include "net.h"

void setup() {
  displayInit(); // Tampilkan boot log pertama
  commsInit();
  displayBootLog("[ OK ] Comms (UART & USB CDC) Initialized");

  netInit(); // Inisiasi Wi-Fi & Web OTA server

  displayBootLog("[ WAIT ] Negotiating Amplifier Link...");
  delay(1000); // Simulasi tunggu sebentar
}

void loop() {
  uint32_t now = millis();
  commsTick(now);
  netTick(now);
  displayTick(now);
}
