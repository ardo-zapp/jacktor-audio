#include "comms_handlers.h"
#include "comms_internal.h"
#include "comms_telemetry.h"
#include "config.h"
#include "state.h"
#include "power.h"
#include "sensors.h"
#include "analyzer.h"
#include "main.h"
#include "gaia.h"

#include "cJSON.h"
#include "i2cdev.h"
#include "driver/i2c_master.h"
#include <vector>
#include <cmath>
#include <cstring>

void handleShellCommand(const std::string& line) {
    if (line.empty()) return;
    ledRxPulse();

    if (equalsIgnoreCase(line.c_str(), "help") || line == "?") {
        debugSerial.println("--- Jacktor Service Shell ---");
        debugSerial.println("stats    : Show runtime stats");
        debugSerial.println("reboot   : Restart ESP32");
        debugSerial.println("pwr on/off: Toggle power");
        debugSerial.println("calibrate: Calibrate ADC bias");
        debugSerial.println("scan     : Scan I2C bus");
        debugSerial.println("GAIA Bluetooth Commands:");
        debugSerial.println("  play, pause, stop, next, prev, volup, voldown");
        debugSerial.println("  battery, rssi, apiver, appver, getbtname, getbtpin, setbtname, setbtpin");
        debugSerial.println("  getpreset, setpreset <0-6>, getvolume, changevolume <0-127>");
        debugSerial.println("  getbass, setbass, get3d, set3d, getusereq, setusereq, geteqparam, seteqparam");
        debugSerial.println("  getdevicename, setdevicename, getbootmode, setbootmode, getmoduleid, getpeeraddress");
        debugSerial.println("  getcodec, setcodec, getpower, setpower, getpio, setpio, getaudiosrc, setaudiosrc");
        debugSerial.println("  getpromptlang, setpromptlang, getspeech, setspeech, getdataendpoint, setdataendpoint");
        debugSerial.println("  enterpairing, reboot, factoryreset, deletepdl, playtone, getled, setled");
    } else if (equalsIgnoreCase(line.c_str(), "stats")) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Uptime: %lu mins | Peak: %.1fC | Fault: %s",
                 (unsigned long)stateGetTotalMinutes(), stateGetPeakTemp(), stateGetLastFault());
        debugSerial.println(buf);
    } else if (equalsIgnoreCase(line.c_str(), "reboot")) {
        debugSerial.println("Rebooting...");
        stateRequestReboot(100);
    } else if (line.rfind("pwr-on", 0) == 0 || line.rfind("pwr on", 0) == 0) {
        bool bypass = (line.find("--bypass") != std::string::npos || line.find(" -b") != std::string::npos);
        if (bypass) {
            powerSetRuntimeBypass(true);
            debugSerial.println("Power ON (Temporary Bypass Active)");
        } else {
            debugSerial.println("Power ON");
        }
        powerSetMainRelay(true, PowerChangeReason::Command);
    } else if (equalsIgnoreCase(line.c_str(), "pwr off") || line == "pwr-off") {
        powerSetMainRelay(false, PowerChangeReason::Command);
        debugSerial.println("Power OFF");
    } else if (equalsIgnoreCase(line.c_str(), "spk-on") || equalsIgnoreCase(line.c_str(), "spk on")) {
        powerSetSpeakerPower(true);
        debugSerial.println("Speaker Protector ON");
    } else if (equalsIgnoreCase(line.c_str(), "spk-off") || equalsIgnoreCase(line.c_str(), "spk off")) {
        powerSetSpeakerPower(false);
        debugSerial.println("Speaker Protector OFF");
    } else if (equalsIgnoreCase(line.c_str(), "bt-on") || equalsIgnoreCase(line.c_str(), "bt on")) {
        powerSetBtEnabled(true);
        debugSerial.println("Bluetooth ON");
    } else if (equalsIgnoreCase(line.c_str(), "bt-off") || equalsIgnoreCase(line.c_str(), "bt off")) {
        powerSetBtEnabled(false);
        debugSerial.println("Bluetooth OFF");
    } else if (equalsIgnoreCase(line.c_str(), "audio-on") || line == "audio-relay-on") {
        powerSetAudioRelay(true);
        debugSerial.println("Audio Relay ON");
    } else if (equalsIgnoreCase(line.c_str(), "audio-off") || line == "audio-relay-off") {
        powerSetAudioRelay(false);
        debugSerial.println("Audio Relay OFF");
    } else if (equalsIgnoreCase(line.c_str(), "calibrate")) {
        analyzerCalibrateAdc();
    } else if (equalsIgnoreCase(line.c_str(), "scan")) {
        debugSerial.println("Scanning I2C...");
        i2c_master_bus_handle_t bus_handle = NULL;
        if (i2cdev_get_shared_handle(I2C_NUM_0, (void**)&bus_handle) == ESP_OK && bus_handle != NULL) {
            for (uint8_t i = 1; i < 127; i++) {
                if (i2c_master_probe(bus_handle, i, 50) == ESP_OK) {
                    debugSerial.printf("Found: 0x%02X\n", i);
                }
            }
        }
    } else if (equalsIgnoreCase(line.c_str(), "deviceinfo")) {
        handleCmdDeviceInfo(nullptr);
    } else if (equalsIgnoreCase(line.c_str(), "setup") || equalsIgnoreCase(line.c_str(), "selftest")) {
        cJSON *b = cJSON_CreateBool(true);
        handleCmdSelftest(b);
        cJSON_Delete(b);
    } else {
        gaiaProcessConsoleCommand(line.c_str());
    }
}

void handleJsonLine(const std::string &line, uart_port_t srcPort) {
  if (stateIsRebootPending()) return;

  cJSON *doc = cJSON_Parse(line.c_str());
  if (!doc) return; // Invalid JSON / noise ignored

  ledRxPulse(); // Valid JSON packet received!

  cJSON *typeItem = cJSON_GetObjectItem(doc, "type");
  const char *type = (typeItem && cJSON_IsString(typeItem)) ? typeItem->valuestring : "";

  if (strcmp(type, "log") == 0 || strcmp(type, "commands_list") == 0) {
    char *out = cJSON_PrintUnformatted(doc);
    if (out) {
      debugSerial.println(out);
      free(out);
    }
    cJSON_Delete(doc);
    return;
  }

  if (strcmp(type, "get_commands") == 0 || strcmp(type, "get_command_list") == 0) {
    if (srcPort != UART_NUM_0) {
      cJSON_Delete(doc);
      return;
    }
    struct CommandHelpItem {
      const char *cmd;
      const char *desc;
    };
    struct CommandCategoryDef {
      const char *name;
      const char *desc;
      const CommandHelpItem *items;
      size_t count;
    };

    static const CommandHelpItem pwr_items[] = {
      {"pwr-on", "pwr-on [--bypass|-b] : Turn main power ON (Bypass protection)"},
      {"pwr-off", "pwr-off : Turn main power OFF (Standby mode)"},
      {"audio-on", "audio-on / audio-relay-on : Enable amplifier audio output relay"},
      {"audio-off", "audio-off / audio-relay-off : Disable amplifier audio output relay (Mute)"},
      {"spk-on", "spk-on : Enable speaker protector circuit & monitoring"},
      {"spk-off", "spk-off : Disable speaker protector circuit (bypass)"},
      {"sleep", "sleep <minutes> : Set auto power-off sleep timer"}
    };
    static const CommandHelpItem input_items[] = {
      {"bt-on", "bt-on : Enable Bluetooth audio input"},
      {"bt-off", "bt-off : Disable Bluetooth audio input (AUX fallback)"}
    };
    static const CommandHelpItem ana_items[] = {
      {"l-gain", "l-gain <val> : Set Left channel gain multiplier (real-time, temporary)"},
      {"r-gain", "r-gain <val> : Set Right channel gain multiplier (real-time, temporary)"},
      {"l-noise", "l-noise <val> : Set Left channel noise gate threshold (real-time, temporary)"},
      {"r-noise", "r-noise <val> : Set Right channel noise gate threshold (real-time, temporary)"},
      {"gain", "gain <val> : Set Left & Right channel gain multiplier"},
      {"noise", "noise <val> : Set Left & Right channel noise gate threshold"},
      {"ana-get", "ana-get : Query audio spectrum analyzer status"},
      {"ana-mode", "ana-mode <fft|vu> : Set spectrum analyzer visualization mode"},
      {"ana-bands", "ana-bands <8..64> : Set FFT bands count"},
      {"calibrate", "calibrate : Calibrate ADC zero-signal bias offset"}
    };
    static const CommandHelpItem fan_items[] = {
      {"fan-auto", "fan-auto : Enable automatic thermal fan control"},
      {"fan-custom", "fan-custom : Override thermal control with custom fan duty"},
      {"fan-duty", "fan-duty <0-1023> : Set custom fan PWM duty cycle"}
    };
    static const CommandHelpItem smps_items[] = {
      {"smps-on", "smps-on : Enable normal SMPS protection monitoring"},
      {"smps-off", "smps-off : Bypass SMPS protection monitoring"},
      {"smps-cutoff", "smps-cutoff <volts> : Set SMPS low voltage cutoff threshold"}
    };
    static const CommandHelpItem tone_items[] = {
      {"tone", "tone -f <hz> -l <ms> -r <repeats> -d <delay> : Play buzzer tone (Linux beep syntax)"},
      {"tone-click", "tone-click : Play short UI click tone"},
      {"tone-test", "tone-test : Run pitch frequency audio test"}
    };
    static const CommandHelpItem gaia_items[] = {
      {"play", "play : Resume media playback"},
      {"pause", "pause : Pause media playback"},
      {"stop", "stop : Stop media playback"},
      {"mute", "mute : Mute or unmute Bluetooth AVRCP audio playback"},
      {"next", "next : Skip to next audio track"},
      {"prev", "prev : Return to previous audio track"},
      {"volup", "volup : Increase Bluetooth volume"},
      {"voldown", "voldown : Decrease Bluetooth volume"},
      {"battery", "battery : Query Bluetooth device battery level"},
      {"rssi", "rssi : Query Bluetooth link signal strength (RSSI)"},
      {"apiver", "apiver : Query GAIA protocol API version"},
      {"appver", "appver : Query QCC firmware application version"},
      {"reboot", "reboot : Soft reboot QCC Bluetooth module"},
      {"factoryreset", "factoryreset : Reset QCC module to factory defaults"},
      {"deletepdl", "deletepdl : Clear Bluetooth paired device list"},
      {"getbtname", "getbtname : Query Bluetooth broadcast name"},
      {"setbtname", "setbtname <name> : Set Bluetooth broadcast name"},
      {"getbtpin", "getbtpin : Query Bluetooth legacy PIN"},
      {"setbtpin", "setbtpin <pin> : Set Bluetooth legacy PIN"},
      {"getdevicename", "getdevicename : Query connected device name"},
      {"setdevicename", "setdevicename <name> : Set device name"},
      {"getbootmode", "getbootmode : Query QCC boot mode"},
      {"setbootmode", "setbootmode <mode> : Set QCC boot mode"},
      {"getmoduleid", "getmoduleid : Query QCC hardware module ID"},
      {"getpeeraddress", "getpeeraddress : Query TWS peer Bluetooth address"},
      {"getpreset", "getpreset : Query active hardware EQ preset index"},
      {"setpreset", "setpreset <0-6> : Select hardware EQ preset (0=Off, 1=Bass, etc.)"},
      {"getvolume", "getvolume : Query current QCC volume level"},
      {"changevolume", "changevolume <0-127> : Set absolute QCC volume level"},
      {"getbass", "getbass : Query Bass Boost feature status"},
      {"setbass", "setbass <0|1> : Enable or disable Bass Boost"},
      {"get3d", "get3d : Query 3D Audio Enhancement status"},
      {"set3d", "set3d <0|1> : Enable or disable 3D Audio Enhancement"},
      {"getusereq", "getusereq : Query User Parametric EQ state"},
      {"setusereq", "setusereq <0|1> : Enable or disable User Parametric EQ"},
      {"geteqparam", "geteqparam <bank> <param> : Query EQ parameter value"},
      {"seteqparam", "seteqparam <bank> <param> <val> : Set EQ parameter value"},
      {"playtone", "playtone <tone_id> : Trigger QCC internal ringtone ID"},
      {"getled", "getled : Query QCC LED control status"},
      {"setled", "setled <0|1> : Enable or disable QCC LED control"},
      {"regnotif", "regnotif <event_id> : Register for GAIA event notifications"},
      {"cancelnotif", "cancelnotif <event_id> : Cancel GAIA event notifications"},
      {"findremote", "findremote <0-2> : Set Find My Remote audio alert level"},
      {"gettwschan", "gettwschan <0|1> : Query TWS channel configuration"},
      {"settwschan", "settwschan <0|1> <0-3> : Set TWS channel configuration"},
      {"gettwsvol", "gettwsvol <0|1> : Query TWS peer volume"},
      {"settwsvol", "settwsvol <0|1> <0-127> : Set TWS peer volume"},
      {"getcodec", "getcodec : Query active Bluetooth audio codec"},
      {"setcodec", "setcodec <0-7> : Set active Bluetooth audio codec"},
      {"getpower", "getpower : Query QCC power state"},
      {"setpower", "setpower <0|1> : Set QCC power state"},
      {"getpio", "getpio : Query QCC PIO status"},
      {"setpio", "setpio <mask> <state> : Set QCC PIO output pins"},
      {"getaudiosrc", "getaudiosrc : Query active audio input source"},
      {"setaudiosrc", "setaudiosrc <0-5> : Set active audio input source"},
      {"enterpairing", "enterpairing : Force QCC Bluetooth into discoverable pairing mode"},
      {"getpromptlang", "getpromptlang : Query voice prompt language"},
      {"setpromptlang", "setpromptlang <id> : Set voice prompt language"},
      {"getspeech", "getspeech : Query speech recognition feature status"},
      {"setspeech", "setspeech <0|1> : Enable or disable speech recognition"},
      {"getdataendpoint", "getdataendpoint : Query data endpoint configuration"},
      {"setdataendpoint", "setdataendpoint <0|1> : Set data endpoint configuration"}
    };
    static const CommandHelpItem rtc_items[] = {
      {"rtc-sync", "rtc-sync : Synchronize DS3231 RTC hardware clock with host time"}
    };
    static const CommandHelpItem system_items[] = {
      {"setup", "setup : Run full amplifier hardware setup and self-diagnostics sequence"},
      {"deviceinfo", "deviceinfo : Display detailed MCU, sensor, and hardware status"},
      {"stats", "stats : Display runtime uptime, peak temperature, and fault history"},
      {"scan", "scan : Scan I2C bus for connected hardware devices"},
      {"reset", "reset : Perform software reset / reboot of the amplifier MCU"},
      {"nvs-reset", "nvs-reset : Erase and restore non-volatile storage (NVS) defaults"}
    };

    static const CommandCategoryDef amp_categories[] = {
      {"power", "Power & relay management", pwr_items, sizeof(pwr_items)/sizeof(pwr_items[0])},
      {"input", "Audio input routing & source selection", input_items, sizeof(input_items)/sizeof(input_items[0])},
      {"ana", "Audio spectrum analyzer calibration & settings", ana_items, sizeof(ana_items)/sizeof(ana_items[0])},
      {"fan", "Cooling fan & thermal management", fan_items, sizeof(fan_items)/sizeof(fan_items[0])},
      {"smps", "SMPS power supply protection & cutoff", smps_items, sizeof(smps_items)/sizeof(smps_items[0])},
      {"tone", "Buzzer tone & sound effects", tone_items, sizeof(tone_items)/sizeof(tone_items[0])},
      {"gaia", "QCC Bluetooth & GAIA protocol controls", gaia_items, sizeof(gaia_items)/sizeof(gaia_items[0])},
      {"rtc", "Real-time clock management", rtc_items, sizeof(rtc_items)/sizeof(rtc_items[0])},
      {"system", "MCU system configuration & diagnostic commands", system_items, sizeof(system_items)/sizeof(system_items[0])}
    };

    // 1. Send Amplifier's supported commands to USB (debugSerial) by direct streaming (zero-heap/zero-recursion)
    if (takeUartTxMutex(pdMS_TO_TICKS(500))) {
      debugSerial.print("{\"ver\":\"1\",\"type\":\"commands_list\",\"origin\":\"AMPLIFIER\",\"categories\":[");
      for (size_t c = 0; c < sizeof(amp_categories)/sizeof(amp_categories[0]); c++) {
        if (c > 0) debugSerial.print(",");
        const CommandCategoryDef &catDef = amp_categories[c];
        debugSerial.print("{\"name\":\"");
        debugSerial.print(catDef.name);
        debugSerial.print("\",\"desc\":\"");
        debugSerial.print(catDef.desc);
        debugSerial.print("\",\"items\":[");
        for (size_t i = 0; i < catDef.count; i++) {
          if (i > 0) debugSerial.print(",");
          debugSerial.print("{\"cmd\":\"");
          debugSerial.print(catDef.items[i].cmd);
          debugSerial.print("\",\"desc\":\"");
          debugSerial.print(catDef.items[i].desc);
          debugSerial.print("\"}");
        }
        debugSerial.print("]}");
      }
      debugSerial.print("],\"commands\":[");
      bool firstCmd = true;
      for (size_t c = 0; c < sizeof(amp_categories)/sizeof(amp_categories[0]); c++) {
        const CommandCategoryDef &catDef = amp_categories[c];
        for (size_t i = 0; i < catDef.count; i++) {
          if (!firstCmd) debugSerial.print(",");
          firstCmd = false;
          debugSerial.print("\"");
          debugSerial.print(catDef.items[i].cmd);
          debugSerial.print("\"");
        }
      }
      debugSerial.print(",\"audio-relay-on\",\"audio-relay-off\",\"spk-pwr-on\",\"spk-pwr-off\",\"beep\",\"buzz\",\"buzz-click\",\"buzz-test\",\"gain-l\",\"gain-r\",\"noise-l\",\"noise-r\"");
      debugSerial.println("]}");
      giveUartTxMutex();
    }

    // 2. Forward get_commands request to Panel over UART2 (linkSerial) only if request didn't come from UART2
    if (srcPort != UART_NUM_2) {
      linkSerial.println("{\"type\":\"get_commands\"}");
    }

    cJSON_Delete(doc);
    return;
  }

  if (strcmp(type, "ping") == 0) {
    handleCmdPing(doc);
    cJSON_Delete(doc);
    return;
  }
  if (strcmp(type, "analyzer") == 0) {
    handleAnalyzerJson(doc);
    cJSON_Delete(doc);
    return;
  }
  if (strcmp(type, "cmd") != 0 && strcmp(type, "command") != 0) {
    cJSON_Delete(doc);
    return;
  }

  cJSON *cmd = cJSON_GetObjectItem(doc, "cmd");
  if (!cmd || !cJSON_IsObject(cmd)) {
    cJSON_Delete(doc);
    return;
  }

  cJSON *item = NULL;
  if ((item = cJSON_GetObjectItem(cmd, "power")) != NULL) handleCmdPower(item);
  if ((item = cJSON_GetObjectItem(cmd, "sleep_timer")) != NULL) handleCmdSleepTimer(item);
  if ((item = cJSON_GetObjectItem(cmd, "bt")) != NULL) handleCmdBt(item);
  if ((item = cJSON_GetObjectItem(cmd, "audio_relay")) != NULL) handleCmdAudioRelay(item);
  if ((item = cJSON_GetObjectItem(cmd, "audio_active")) != NULL) handleCmdAudioRelay(item);
  if ((item = cJSON_GetObjectItem(cmd, "display_text")) != NULL) handleCmdDisplayText(item);
  if ((item = cJSON_GetObjectItem(cmd, "spk_pwr")) != NULL) handleCmdSpkPwr(item);
  if ((item = cJSON_GetObjectItem(cmd, "spk")) != NULL) handleCmdSpkPwr(item);
  if ((item = cJSON_GetObjectItem(cmd, "smps_bypass")) != NULL) handleCmdSmpsBypass(item);
  if ((item = cJSON_GetObjectItem(cmd, "smps_cut")) != NULL) handleCmdSmpsCut(item);
  if ((item = cJSON_GetObjectItem(cmd, "smps_rec")) != NULL) handleCmdSmpsRec(item);
  if ((item = cJSON_GetObjectItem(cmd, "bt_autooff")) != NULL) handleCmdBtAutoOff(item);
  if ((item = cJSON_GetObjectItem(cmd, "auto_sense_en")) != NULL) handleCmdAutoSenseEn(item);
  if ((item = cJSON_GetObjectItem(cmd, "auto_sense_timeout")) != NULL) handleCmdAutoSenseTimeout(item);
  if ((item = cJSON_GetObjectItem(cmd, "auto_sense_thresh")) != NULL) handleCmdAutoSenseThresh(item);
  if ((item = cJSON_GetObjectItem(cmd, "tune")) != NULL) handleCmdTune(item);
  if ((item = cJSON_GetObjectItem(cmd, "media")) != NULL) handleCmdMedia(item);
  if ((item = cJSON_GetObjectItem(cmd, "fan_mode")) != NULL) handleCmdFanMode(item);
  if ((item = cJSON_GetObjectItem(cmd, "fan_duty")) != NULL) handleCmdFanDuty(item);
  if ((item = cJSON_GetObjectItem(cmd, "rtc_set_epoch")) != NULL) handleCmdRtcSetEpoch(item);
  if ((item = cJSON_GetObjectItem(cmd, "ota_begin")) != NULL) handleCmdOtaBegin(item);
  if ((item = cJSON_GetObjectItem(cmd, "ota_write")) != NULL) handleCmdOtaWrite(item);
  if ((item = cJSON_GetObjectItem(cmd, "ota_end")) != NULL) handleCmdOtaEnd(item);
  if ((item = cJSON_GetObjectItem(cmd, "ota_abort")) != NULL) handleCmdOtaAbort(item);
  if ((item = cJSON_GetObjectItem(cmd, "tone")) != NULL) handleCmdTone(item);
  if ((item = cJSON_GetObjectItem(cmd, "buzz")) != NULL) handleCmdTone(item);
  if ((item = cJSON_GetObjectItem(cmd, "nvs_reset")) != NULL) handleCmdNvsReset(item);
  if ((item = cJSON_GetObjectItem(cmd, "factory_reset")) != NULL) handleCmdFactoryReset(item);
  if ((item = cJSON_GetObjectItem(cmd, "alarm_stop")) != NULL) handleCmdAlarmStop(item);
  if ((item = cJSON_GetObjectItem(cmd, "btn_lock")) != NULL) handleCmdBtnLock(item);
  if ((item = cJSON_GetObjectItem(cmd, "bt_continue")) != NULL) handleCmdBtContinue(item);
  if ((item = cJSON_GetObjectItem(cmd, "stats")) != NULL) handleCmdGetStats(item);
  if ((item = cJSON_GetObjectItem(cmd, "ping")) != NULL) handleCmdPing(item);
  if ((item = cJSON_GetObjectItem(cmd, "deviceinfo")) != NULL) handleCmdDeviceInfo(item);
  if ((item = cJSON_GetObjectItem(cmd, "setup")) != NULL) handleCmdSelftest(item);
  if ((item = cJSON_GetObjectItem(cmd, "selftest")) != NULL) handleCmdSelftest(item);

  if ((item = cJSON_GetObjectItem(cmd, "play")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_PLAY); }
  if ((item = cJSON_GetObjectItem(cmd, "pause")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_PAUSE); }
  if ((item = cJSON_GetObjectItem(cmd, "stop")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_STOP); }
  if ((item = cJSON_GetObjectItem(cmd, "next")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_FORWARD); }
  if ((item = cJSON_GetObjectItem(cmd, "prev")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_BACKWARD); }
  if ((item = cJSON_GetObjectItem(cmd, "volup")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_VOLUME_UP); }
  if ((item = cJSON_GetObjectItem(cmd, "voldown")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendAvrcpControl(AVRCP_VOLUME_DOWN); }
  if ((item = cJSON_GetObjectItem(cmd, "getpreset")) != NULL) { gaiaSendCommand(CMD_GET_EQ_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "setpreset")) != NULL) { uint8_t p = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_EQ_CONTROL, &p, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getvolume")) != NULL) { gaiaSendCommand(CMD_GET_VOLUME); }
  if ((item = cJSON_GetObjectItem(cmd, "changevolume")) != NULL) { uint8_t vol = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_CHANGE_VOLUME, &vol, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "enterpairing")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_ENTER_BLUETOOTH_PAIRING_MODE); }
  if ((item = cJSON_GetObjectItem(cmd, "battery")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_CURRENT_BATTERY_LEVEL); }
  if ((item = cJSON_GetObjectItem(cmd, "rssi")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_CURRENT_RSSI); }
  if ((item = cJSON_GetObjectItem(cmd, "apiver")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_API_VERSION); }
  if ((item = cJSON_GetObjectItem(cmd, "appver")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_APPLICATION_VERSION); }
  if ((item = cJSON_GetObjectItem(cmd, "reboot")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_DEVICE_RESET); }
  if ((item = cJSON_GetObjectItem(cmd, "factoryreset")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_FACTORY_DEFAULT_RESET); }
  if ((item = cJSON_GetObjectItem(cmd, "deletepdl")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_DELETE_PDL); }
  if ((item = cJSON_GetObjectItem(cmd, "getbtname")) != NULL) { if (cJSON_IsTrue(item)) { uint8_t payload[2] = {0x00, 0xC8}; gaiaSendCommand(CMD_RETRIEVE_PS_KEY, payload, 2); } }
  if ((item = cJSON_GetObjectItem(cmd, "getbtpin")) != NULL) { if (cJSON_IsTrue(item)) { uint8_t payload[2] = {0x00, 0xFA}; gaiaSendCommand(CMD_RETRIEVE_PS_KEY, payload, 2); } }
  if ((item = cJSON_GetObjectItem(cmd, "getbass")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_BASS_BOOST_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "setbass")) != NULL) { uint8_t b = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_BASS_BOOST_CONTROL, &b, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "get3d")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_3D_ENHANCEMENT_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "set3d")) != NULL) { uint8_t b = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_3D_ENHANCEMENT_CONTROL, &b, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getusereq")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_USER_EQ_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "setusereq")) != NULL) { uint8_t u = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_USER_EQ_CONTROL, &u, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getdevicename")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_DEVICE_NAME); }
  if ((item = cJSON_GetObjectItem(cmd, "getbootmode")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_BOOT_MODE); }
  if ((item = cJSON_GetObjectItem(cmd, "getmoduleid")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_MODULE_ID); }
  if ((item = cJSON_GetObjectItem(cmd, "getpeeraddress")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_PEER_ADDRESS); }
  if ((item = cJSON_GetObjectItem(cmd, "getcodec")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_CODEC); }
  if ((item = cJSON_GetObjectItem(cmd, "setcodec")) != NULL) { uint8_t c = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_CODEC, &c, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getpower")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_POWER_STATE); }
  if ((item = cJSON_GetObjectItem(cmd, "setpower")) != NULL) {
    uint8_t p = (uint8_t)item->valuedouble;
    if (p == 0) {
      if (gaiaIsConnected()) gaiaSendCommand(CMD_SET_POWER_STATE, &p, 1);
      powerSetBtEnabled(false);
    } else {
      powerSetBtEnabled(true);
      if (gaiaIsConnected()) gaiaSendCommand(CMD_SET_POWER_STATE, &p, 1);
    }
  }
  if ((item = cJSON_GetObjectItem(cmd, "getpio")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_PIO_STATE); }
  if ((item = cJSON_GetObjectItem(cmd, "getaudiosrc")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_AUDIO_SOURCE); }
  if ((item = cJSON_GetObjectItem(cmd, "setaudiosrc")) != NULL) { uint8_t a = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_AUDIO_SOURCE, &a, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getpromptlang")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_AUDIO_PROMPT_LANGUAGE); }
  if ((item = cJSON_GetObjectItem(cmd, "setpromptlang")) != NULL) { uint8_t l = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_CHANGE_AUDIO_PROMPT_LANGUAGE, &l, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getspeech")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_SPEECH_RECOGNITION_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "setspeech")) != NULL) { uint8_t s = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_SPEECH_RECOGNITION_CONTROL, &s, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getdataendpoint")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_DATA_ENDPOINT_MODE); }
  if ((item = cJSON_GetObjectItem(cmd, "setdataendpoint")) != NULL) { uint8_t d = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_DATA_ENDPOINT_MODE, &d, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "getled")) != NULL) { if (cJSON_IsTrue(item)) gaiaSendCommand(CMD_GET_LED_CONTROL); }
  if ((item = cJSON_GetObjectItem(cmd, "setled")) != NULL) { uint8_t l = (uint8_t)item->valuedouble; gaiaSendCommand(CMD_SET_LED_CONTROL, &l, 1); }
  if ((item = cJSON_GetObjectItem(cmd, "gaia")) != NULL && cJSON_IsString(item)) { gaiaProcessConsoleCommand(item->valuestring); }

  cJSON_Delete(doc);
}
