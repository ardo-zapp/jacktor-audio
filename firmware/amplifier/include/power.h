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
bool powerSmpsTripLatched();
bool powerSmpsHwFaultLatched();
bool powerOtpFault();

void powerSetSleepTimer(uint32_t minutes);
uint32_t powerGetSleepRemainingMinutes();

void powerSmpsStartSoftstart(uint32_t msDelay);
bool powerSmpsSoftstartActive();
bool powerSmpsIsValid();