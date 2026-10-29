#include "power_fault.h"
#include "config.h"
#include "comms.h"
#include "state.h"

static bool smpsCutActive = false;
static bool smpsFaultLatched = false;
static uint32_t smpsFaultGraceUntilMs = 0;

void powerFaultInit() {
  smpsCutActive = false;
  smpsFaultLatched = false;
  smpsFaultGraceUntilMs = 0;
}

void powerFaultTick(uint32_t now, bool isRelayOn, bool inSoftstart, float voltage, float cutV, float recV) {
  if (!FEAT_SMPS_PROTECT_ENABLE || !isRelayOn || inSoftstart || stateSmpsBypass() || smpsFaultLatched || (stateSelfTestGetStatus() == SelfTestStatus::RUNNING)) {
    if (!isRelayOn || inSoftstart || stateSmpsBypass() || (stateSelfTestGetStatus() == SelfTestStatus::RUNNING)) {
      smpsFaultGraceUntilMs = 0;
    }
    return;
  }

  bool noPower = (voltage == 0.0f);
  bool lowVolt = (voltage > 0.0f && voltage < cutV);
  bool inFault = (noPower || lowVolt);

  if (inFault) {
    if (smpsFaultGraceUntilMs == 0) {
      smpsFaultGraceUntilMs = now + 1000;
    }
    if (now > smpsFaultGraceUntilMs) {
      smpsFaultLatched = true;
      smpsCutActive = false;
      stateSetLastFault(noPower ? "smps_no_power" : "smps_undervoltage");
      commsLog("error", noPower ? "smps_no_power_trip" : "smps_undervoltage_trip");
    } else {
      smpsCutActive = true;
    }
  } else {
    smpsFaultGraceUntilMs = 0;
    if (smpsCutActive && voltage >= recV) {
      smpsCutActive = false;
    }
  }
}

bool powerFaultSmpsIsCutActive() { return smpsCutActive; }
bool powerFaultSmpsIsTripLatched() { return smpsFaultLatched; }
void powerFaultSmpsReset() {
  smpsCutActive = false;
  smpsFaultLatched = false;
  smpsFaultGraceUntilMs = 0;
}
void powerFaultSetSmpsCutActive(bool active) { smpsCutActive = active; }
