#pragma once
#include <cstdint>

void powerHwInit();
void powerHwSetRelaySmps(bool on);
void powerHwSetRelayAudio(bool on);
void powerHwSetSpeakerPins(bool powerOn);
void powerHwSetBtPins(bool enable);
bool powerHwReadBtStatus();
bool powerHwReadBtReady();
uint32_t powerHwGetBtLastLowMs();
void powerHwResetBtLastLowMs();
void powerHwSetFanDuty(uint16_t duty);
uint16_t powerHwGetFanDuty();

bool powerHwReadSmpsFault();
bool powerHwReadSpkProtect();
