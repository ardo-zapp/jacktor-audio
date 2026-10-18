#pragma once
#include <cstdint>

enum class PowerState : uint8_t { On, Standby };
enum class PowerChangeReason : uint8_t { Button, Command, SmpsFaultDetect, FactoryReset };

typedef void (*PowerStateListener)(PowerState prev, PowerState now, PowerChangeReason reason);

void powerInit();
void powerTick(const uint32_t now);
void powerSetMainRelay(bool on, PowerChangeReason reason);
bool powerMainRelay();
void powerRegisterStateListener(PowerStateListener listener);
PowerState powerCurrentState();

void powerSetAudioRelay(bool on);
bool powerGetAudioRelay();
void powerSetSpeakerPower(bool on);
bool powerGetSpeakerPower();
void powerSetAlarmsSilenced(bool silenced);
bool powerAlarmsSilenced();
void powerSetBtEnabled(bool en, bool persist = true);
bool powerBtEnabled();
bool powerBtMode();
bool powerIsInitializing();
uint32_t powerGetBtPowerOnMs();
void powerNotifyBtConnected();
void powerHardCycleBt();
bool powerBtHardCycling();
bool powerBtFailedPerm();
void powerClearBtFailedPerm();
void powerSetBtFailedPerm(bool failed);
void powerBypassBtInitializing();
void powerCompleteSelfTest();
void powerApplyBtHardware();
void powerSetOtaActive(bool on);
bool powerSpkProtectFault();
bool powerQccProtectFault();
const char* powerInputModeStr();
bool powerSmpsTripLatched();
bool powerSmpsHwFaultLatched();
bool powerOtpFault();

void powerSetRuntimeBypass(bool active);
bool powerIsRuntimeBypassActive(void);

void powerSetSleepTimer(uint32_t minutes);
uint32_t powerGetSleepRemainingMinutes();

void powerSmpsStartSoftstart(uint32_t msDelay);
bool powerSmpsSoftstartActive();
bool powerSmpsIsValid();