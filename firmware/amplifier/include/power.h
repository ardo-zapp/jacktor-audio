#pragma once
#include <Arduino.h>



enum class PowerState : uint8_t {
  Standby = 0,
  On      = 1,
};

enum class PowerChangeReason : uint8_t {
  Unknown = 0,
  Button,
  Command,
  PcDetect,
  FactoryReset,
};

using PowerStateListener = void (*)(PowerState prev, PowerState now, PowerChangeReason reason);
// Inisialisasi GPIO (relay, fan PWM, BT, selector/power speaker, monitor, dll.)
void powerInit();

// Tick rutin (panggil di loop)
void powerTick(uint32_t now);

// Listener perubahan state power utama
void powerRegisterStateListener(PowerStateListener listener);

// Relay utama (aktif HIGH/LOW sesuai config)
void powerSetMainRelay(bool on, PowerChangeReason reason = PowerChangeReason::Unknown);
bool powerMainRelay();
PowerState powerCurrentState();

// Speaker: selector (BIG/SMALL) & power (supply speaker protector)
void powerSetSpeakerSelect(bool big);
bool powerGetSpeakerSelectBig();

void powerSetSpeakerPower(bool on);
bool powerGetSpeakerPower();

// Bluetooth: enable/disable modul dan status mode (BT vs AUX dari LED status)
void powerSetBtEnabled(bool en);
bool powerBtEnabled();
bool powerBtMode();  // true=BT mode (LED status LOW), false=AUX

// OTA guard (true → auto-power berbasis PC detect diabaikan)
void powerSetOtaActive(bool on);

// Fault monitor speaker protector LED
bool powerSpkProtectFault();

// Input mode string (untuk telemetri/UI ringkas)
const char* powerInputModeStr();

// PC detect status (untuk telemetri rt/hz1)
bool powerPcDetectLevelActive();      // true jika level aktif (LOW pada config default)
bool powerPcDetectArmed();            // true jika status debounced menganggap PC ON
uint32_t powerPcDetectLastChangeMs(); // millis() saat raw berubah terakhir
bool powerSmpsTripLatched();
void powerSmpsStartSoftstart(uint32_t msDelay);
bool powerSmpsSoftstartActive();