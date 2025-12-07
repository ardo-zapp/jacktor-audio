#pragma once
#include <Arduino.h>

enum class PowerState : uint8_t { On, Standby };
enum class PowerChangeReason : uint8_t { Button, Command, PcDetect, FactoryReset };

typedef void (*PowerStateListener)(PowerState prev, PowerState now, PowerChangeReason reason);

void powerInit();
void powerTick(const uint32_t now);
void powerSetMainRelay(bool on, PowerChangeReason reason);
bool powerMainRelay();
void powerRegisterStateListener(PowerStateListener listener);
PowerState powerCurrentState();

void powerSetSpeakerSelect(bool big);
bool powerGetSpeakerSelectBig();
void powerSetSpeakerPower(bool on);
bool powerGetSpeakerPower();
void powerSetBtEnabled(bool en);
bool powerBtEnabled();
bool powerBtMode();
void powerSetOtaActive(bool on);
bool powerSpkProtectFault();
const char* powerInputModeStr();
bool powerPcDetectLevelActive();
bool powerPcDetectArmed();
uint32_t powerPcDetectLastChangeMs();
bool powerSmpsTripLatched();

void powerSmpsStartSoftstart(uint32_t msDelay);
bool powerSmpsSoftstartActive();
bool powerSmpsIsValid();