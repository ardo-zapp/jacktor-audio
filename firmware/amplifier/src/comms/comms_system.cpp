#include "comms_handlers.h"
#include "comms_internal.h"
#include "comms_telemetry.h"
#include "config.h"
#include "state.h"
#include "power.h"
#include "power_hw.h"
#include "sensors.h"
#include "tm1637.h"
#include "analyzer.h"
#include "main.h"

#include "cJSON.h"
#include <cmath>
#include <cstring>

static void handleRtcSync(uint32_t targetEpoch) {
  uint32_t currentEpoch = 0;
  if (!sensorsGetUnixTime(currentEpoch)) {
    sendLogErrorReason("rtc_sync_failed", "rtc_unavailable");
    return;
  }
  int32_t offset = (int32_t)((int64_t)targetEpoch - (int64_t)currentEpoch);
  if (!sensorsSetUnixTime(targetEpoch)) {
    sendLogErrorReason("rtc_sync_failed", "rtc_set_fail");
    return;
  }
  stateSetLastRtcSync(targetEpoch);
  sendLogInfoOffset(offset);
  forceTel = true;
}

void handleCmdPower(cJSON *item) {
  bool on = false;
  bool bypass = false;

  if (cJSON_IsBool(item)) {
    on = cJSON_IsTrue(item);
  } else if (cJSON_IsObject(item)) {
    cJSON *val = cJSON_GetObjectItem(item, "state");
    if (!val) val = cJSON_GetObjectItem(item, "power");
    if (val && cJSON_IsBool(val)) on = cJSON_IsTrue(val);

    cJSON *bItem = cJSON_GetObjectItem(item, "bypass");
    if (!bItem) bItem = cJSON_GetObjectItem(item, "temp_bypass");
    if (bItem && cJSON_IsBool(bItem)) bypass = cJSON_IsTrue(bItem);
  } else {
    sendAckErr("power", "invalid");
    return;
  }

  if (on && bypass) {
    powerSetRuntimeBypass(true);
  }

  powerSetMainRelay(on, PowerChangeReason::Command);
  sendAckOkBool("power", on, true);
  forceTel = true;
}

void handleCmdSleepTimer(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("sleep_timer", "invalid"); return; }
  uint32_t minutes = (uint32_t)item->valuedouble;
  powerSetSleepTimer(minutes);
  sendAckOkInt("sleep_timer", minutes, true);
  forceTel = true;
}

void handleCmdBt(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("bt", "invalid"); return; }
  bool en = cJSON_IsTrue(item);
  powerSetBtEnabled(en);
  sendAckOkBool("bt", en, true);
  forceTel = true;
}

void handleCmdAudioRelay(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("audio_relay", "invalid"); return; }
  bool on = cJSON_IsTrue(item);
  powerSetAudioRelay(on);
  sendAckOkBool("audio_relay", on, true);
  forceTel = true;
}

void handleCmdDisplayText(cJSON *item) {
  if (cJSON_IsString(item) && item->valuestring) {
    tm1637SetOverrideText(item->valuestring);
    sendAckOkStr("display_text", item->valuestring, false);
  } else {
    tm1637SetOverrideText("");
    sendAckOkStr("display_text", "", false);
  }
}

void handleCmdSpkPwr(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("spk_pwr", "invalid"); return; }
  bool on = cJSON_IsTrue(item);
  powerSetSpeakerPower(on);
  sendAckOkBool("spk_pwr", on, true);
  forceTel = true;
}

void handleCmdSmpsBypass(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("smps_bypass", "invalid"); return; }
  bool en = cJSON_IsTrue(item);
  stateSetSmpsBypass(en);
  sendAckOkBool("smps_bypass", en, true);
  forceTel = true;
}

void handleCmdSmpsCut(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("smps_cut", "invalid"); return; }
  float cut = (float)item->valuedouble;
  if (cut < 30.0f || cut > 70.0f || cut >= stateSmpsRecoveryV()) {
    sendAckErr("smps_cut", "range"); return;
  }
  stateSetSmpsCutoffV(cut);
  sendAckOkFloat("smps_cut", cut, true);
  forceTel = true;
}

void handleCmdSmpsRec(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("smps_rec", "invalid"); return; }
  float rec = (float)item->valuedouble;
  if (rec < 30.0f || rec > 70.0f || rec <= stateSmpsCutoffV()) {
    sendAckErr("smps_rec", "range"); return;
  }
  stateSetSmpsRecoveryV(rec);
  sendAckOkFloat("smps_rec", rec, true);
  forceTel = true;
}

void handleCmdBtAutoOff(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("bt_autooff", "invalid"); return; }
  double valD = item->valuedouble;
  if (valD < 0 || valD > 4294967295.0) { sendAckErr("bt_autooff", "range"); return; }
  uint32_t val = (uint32_t)(valD + 0.5);
  stateSetBtAutoOffMs(val);
  sendAckOkInt("bt_autooff", val, true);
  forceTel = true;
}

void handleCmdAutoSenseEn(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("auto_sense_en", "invalid"); return; }
  bool en = cJSON_IsTrue(item);
  stateSetAutoSenseEnabled(en);
  sendAckOkBool("auto_sense_en", en, true);
  forceTel = true;
}

void handleCmdAutoSenseTimeout(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("auto_sense_timeout", "invalid"); return; }
  uint32_t mins = (uint32_t)item->valuedouble;
  stateSetAutoSenseTimeout(mins);
  sendAckOkInt("auto_sense_timeout", mins, true);
  forceTel = true;
}

void handleCmdAutoSenseThresh(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("auto_sense_thresh", "invalid"); return; }
  uint8_t thr = static_cast<uint8_t>(item->valuedouble);
  stateSetAutoSenseThresh(thr);
  sendAckOkInt("auto_sense_thresh", thr, true);
  forceTel = true;
}


void handleCmdTune(cJSON *item) {
  if (!cJSON_IsObject(item)) { sendAckErr("tune", "invalid"); return; }

  cJSON *kp = cJSON_GetObjectItem(item, "pid_kp");
  if (kp && cJSON_IsNumber(kp)) stateSetPidKp((float)kp->valuedouble);

  cJSON *ki = cJSON_GetObjectItem(item, "pid_ki");
  if (ki && cJSON_IsNumber(ki)) stateSetPidKi((float)ki->valuedouble);

  cJSON *kd = cJSON_GetObjectItem(item, "pid_kd");
  if (kd && cJSON_IsNumber(kd)) stateSetPidKd((float)kd->valuedouble);

  cJSON *sp = cJSON_GetObjectItem(item, "pid_setpoint");
  if (sp && cJSON_IsNumber(sp)) stateSetPidSetpoint((float)sp->valuedouble);

  cJSON *so = cJSON_GetObjectItem(item, "stby_offset");
  if (so && cJSON_IsNumber(so)) stateSetStbyOffset((float)so->valuedouble);

  cJSON *otp = cJSON_GetObjectItem(item, "otp_thresh");
  if (otp && cJSON_IsNumber(otp)) stateSetOtpThresh((float)otp->valuedouble);

  cJSON *sov = cJSON_GetObjectItem(item, "smps_ov");
  if (sov && cJSON_IsNumber(sov)) stateSetSmpsOvThresh((float)sov->valuedouble);

  cJSON *stov = cJSON_GetObjectItem(item, "stby_ov");
  if (stov && cJSON_IsNumber(stov)) stateSetStbyOvThresh((float)stov->valuedouble);

  cJSON *stlow = cJSON_GetObjectItem(item, "stby_low");
  if (stlow && cJSON_IsNumber(stlow)) stateSetStbyLowThresh((float)stlow->valuedouble);

  sendAckOkBool("tune", true, true);
  forceTel = true;
}

void handleCmdSelftest(cJSON *item) {
  if (!cJSON_IsBool(item) || !cJSON_IsTrue(item)) { sendAckErr("setup", "invalid"); return; }
  if (!powerIsOn()) { sendAckErr("setup", "must_be_on"); return; }
  if (stateSelfTestGetStatus() != SelfTestStatus::IDLE) { sendAckErr("setup", "already_running"); return; }

  stateSelfTestStart();
  sendAckOkBool("setup", true, true);
}

void handleCmdMedia(cJSON *item) {
  if (!cJSON_IsObject(item)) { sendAckErr("media", "invalid"); return; }
  sendAckOkBool("media", true, false);
}

void handleCmdFanMode(cJSON *item) {
  if (!cJSON_IsString(item)) { sendAckErr("fan_mode", "invalid"); return; }
  const char* s = item->valuestring;

  if (equalsIgnoreCase(s, "quiet")) {
      stateSetPidKp(1.0f);
      stateSetPidKi(0.02f);
      stateSetPidSetpoint(55.0f);
      stateSetFanMode(FanMode::AUTO);
      sendAckOkStr("fan_preset", "quiet", true);
  } else if (equalsIgnoreCase(s, "normal")) {
      stateSetPidKp(2.0f);
      stateSetPidKi(0.05f);
      stateSetPidSetpoint(45.0f);
      stateSetFanMode(FanMode::AUTO);
      sendAckOkStr("fan_preset", "normal", true);
  } else if (equalsIgnoreCase(s, "performance")) {
      stateSetPidKp(4.0f);
      stateSetPidKi(0.1f);
      stateSetPidSetpoint(35.0f);
      stateSetFanMode(FanMode::AUTO);
      sendAckOkStr("fan_preset", "performance", true);
  } else {
      FanMode mode;
      if (!fanModeFromStr(s, mode)) {
        sendAckErr("fan_mode", "invalid"); return;
      }
      stateSetFanMode(mode);
      sendAckOkStr("fan_mode", fanModeToStr(mode), true);
  }
  forceTel = true;
}

void handleCmdFanDuty(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("fan_duty", "invalid"); return; }
  int duty = (int)std::lround(item->valuedouble);
  if (duty < 0 || duty > 1023) { sendAckErr("fan_duty", "range"); return; }
  stateSetFanCustomDuty((uint16_t)duty);
  sendAckOkInt("fan_duty", duty, true);
  forceTel = true;
}

void handleCmdRtcSetEpoch(cJSON *item) {
  if (!cJSON_IsNumber(item)) { sendAckErr("rtc_set_epoch", "invalid"); return; }
  uint32_t epoch = (uint32_t)item->valuedouble;
  handleRtcSync(epoch);
}

void handleCmdNvsReset(cJSON *item) {
  if (!cJSON_IsBool(item) || !cJSON_IsTrue(item)) { sendAckErr("nvs_reset", "invalid"); return; }
  stateFactoryReset();
  powerSetSpeakerPower(stateSpeakerPowerOn());
  powerSetBtEnabled(stateBtEnabled());
  sendAckOkBool("nvs_reset", true, true);
  forceTel = true;
}

void handleCmdFactoryReset(cJSON *item) {
  if (!cJSON_IsBool(item) || !cJSON_IsTrue(item)) { sendAckErr("factory_reset", "invalid"); return; }
  if (powerIsOn()) { sendAckErr("factory_reset", "system_active"); return; }
  sendAckOkBool("factory_reset", true, true);
  forceTel = true;
  appPerformFactoryReset("FACTORY RESET (UART)", "uart");
}

void handleCmdAlarmStop(cJSON *item) {
  bool stop = cJSON_IsBool(item) ? cJSON_IsTrue(item) : true;
  powerSetAlarmsSilenced(stop);
  sendAckOkBool("alarm_stop", stop, false);
}

void handleCmdBtnLock(cJSON *item) {
  if (!cJSON_IsBool(item)) { sendAckErr("btn_lock", "invalid"); return; }
  bool lock = cJSON_IsTrue(item);
  stateSetBtnLock(lock);
  sendAckOkBool("btn_lock", lock, true);
  forceTel = true;
}

void handleCmdBtContinue(cJSON *item) {
  if (cJSON_IsBool(item) && cJSON_IsTrue(item)) {
    powerBypassBtInitializing();
    sendAckOkBool("bt_continue", true, false);
    forceTel = true;
  }
}

void handleCmdGetStats(cJSON *item) {
  sendStatsReport();
  sendAckOkBool("get_stats", true, false);
}

void handleCmdDeviceInfo(cJSON *item) {
  (void)item;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "deviceinfo");
  cJSON_AddStringToObject(root, "device", "amplifier");
  cJSON_AddStringToObject(root, "fw_name", FW_NAME);
  cJSON_AddStringToObject(root, "fw_ver", FW_VERSION);
  cJSON_AddStringToObject(root, "os", "ESP-IDF v6.0.1 (FreeRTOS)");
  cJSON_AddStringToObject(root, "mcu", "ESP32 (Xtensa LX6 @ 240MHz)");

  float heat = getHeatsinkC();
  if (!std::isnan(heat)) cJSON_AddNumberToObject(root, "cpu_temp_c", heat);

  cJSON *sensors = cJSON_CreateObject();

  cJSON *ds18 = cJSON_CreateObject();
  if (!std::isnan(heat)) {
    cJSON_AddStringToObject(ds18, "status", "OK");
    cJSON_AddNumberToObject(ds18, "temp_c", heat);
  } else {
    cJSON_AddStringToObject(ds18, "status", "NAN_OR_FAULT");
  }
  cJSON_AddItemToObject(sensors, "ds18b20_heatsink", ds18);

  cJSON *rtc = cJSON_CreateObject();
  float rtcT = sensorsGetRtcTempC();
  char timeIso[32];
  bool timeOk = sensorsGetTimeISO(timeIso, sizeof(timeIso));
  cJSON_AddStringToObject(rtc, "status", sensorsIsRtcReady() ? (timeOk ? "OK" : "FAULT") : "SYSTEM_FALLBACK");
  if (!std::isnan(rtcT)) cJSON_AddNumberToObject(rtc, "temp_c", rtcT);
  if (timeOk) cJSON_AddStringToObject(rtc, "time", timeIso);
  cJSON_AddItemToObject(sensors, "ds3231_rtc", rtc);

  cJSON *ads = cJSON_CreateObject();
  float vSmps = getVoltageInstant();
  float vStby = getVoltageStandby();
  float vVdd = getVoltageVdd();
  float vEsp = getVoltageEsp32();
  cJSON_AddStringToObject(ads, "status", "OK");
  cJSON_AddNumberToObject(ads, "v_smps", vSmps);
  cJSON_AddNumberToObject(ads, "v_stby", vStby);
  cJSON_AddNumberToObject(ads, "v_vdd", vVdd);
  cJSON_AddNumberToObject(ads, "v_esp", vEsp);
  cJSON_AddItemToObject(sensors, "ads1115_voltmeter", ads);

  cJSON *smpsF = cJSON_CreateObject();
  bool smpsFault = powerHwReadSmpsFault();
  cJSON_AddStringToObject(smpsF, "status", smpsFault ? "FAULT_DETECTED" : "NORMAL");
  cJSON_AddNumberToObject(smpsF, "gpio", SMPS_FAULT_PIN);
  cJSON_AddItemToObject(sensors, "smps_fault_opto", smpsF);

  cJSON *spkP = cJSON_CreateObject();
  bool spkOk = powerHwReadSpkProtect();
  cJSON_AddStringToObject(spkP, "status", spkOk ? "NORMAL" : "PROTECT_TRIP");
  cJSON_AddNumberToObject(spkP, "gpio", SPK_PROTECT_LED_PIN);
  cJSON_AddItemToObject(sensors, "speaker_protect_opto", spkP);

  cJSON *qcc = cJSON_CreateObject();
  bool qccReady = powerHwReadBtReady();
  cJSON_AddStringToObject(qcc, "status", qccReady ? "ACTIVE" : "INACTIVE");
  cJSON_AddNumberToObject(qcc, "gpio", QCC_PWR_DETECT_PIN);
  cJSON_AddItemToObject(sensors, "qcc5125_power_detect", qcc);

  cJSON *btStat = cJSON_CreateObject();
  bool btConn = powerHwReadBtStatus();
  cJSON_AddStringToObject(btStat, "status", btConn ? "ACTIVE" : "INACTIVE");
  cJSON_AddNumberToObject(btStat, "gpio", BT_STATUS_PIN);
  cJSON_AddItemToObject(sensors, "bt_status_pin", btStat);

  cJSON *adcAudio = cJSON_CreateObject();
  float dcL = analyzerGetDcOffsetL();
  float dcR = analyzerGetDcOffsetR();
  float vAdcL = (dcL * 3.3f) / 4095.0f;
  float vAdcR = (dcR * 3.3f) / 4095.0f;
  cJSON_AddNumberToObject(adcAudio, "raw_l", roundf(dcL * 10.0f) / 10.0f);
  cJSON_AddNumberToObject(adcAudio, "raw_r", roundf(dcR * 10.0f) / 10.0f);
  cJSON_AddNumberToObject(adcAudio, "v_adc_l", roundf(vAdcL * 1000.0f) / 1000.0f);
  cJSON_AddNumberToObject(adcAudio, "v_adc_r", roundf(vAdcR * 1000.0f) / 1000.0f);
  cJSON_AddNumberToObject(adcAudio, "gpio_l", 39);
  cJSON_AddNumberToObject(adcAudio, "gpio_r", 36);
  cJSON_AddItemToObject(sensors, "adc_audio", adcAudio);

  cJSON_AddItemToObject(root, "sensors", sensors);

  char *out = cJSON_PrintUnformatted(root);
  if (out) {
    linkSerial.println(out);
    debugSerial.println(out);
    cJSON_free(out);
  }
  cJSON_Delete(root);
  sendAckOkBool("deviceinfo", true, false);
}

void handleCmdPing(cJSON *item) {
  sendAckOkBool("ping", true, false);
}
