#include "gaia.h"
#include "comms.h"
#include "comms_internal.h"
#include "buzzer.h"
#include "power_hw.h"
#include "power.h"
#include "state.h"
#include <vector>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "tm1637.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* targetName = "Jacktor Audio";
static uint8_t targetAddress[6] = {0x53, 0x4A, 0x52, 0xFE, 0x00, 0xAF};

enum GaiaConnState {
    GAIA_CONN_IDLE,
    GAIA_CONN_SCANNING,
    GAIA_CONN_SDP_DISCOVERY,
    GAIA_CONN_CONNECTING,
    GAIA_CONN_CONNECTED
};

static GaiaConnState s_connState = GAIA_CONN_IDLE;
static uint32_t s_sppHandle = 0;
static bool s_isConnected = false;
static uint32_t s_scanStartMs = 0;
static uint32_t s_lastScanStartMs = 0;
static bool s_targetFound = false;

static uint8_t s_connFailures = 0;
static uint8_t s_powerCyclesCount = 0;
static bool s_initialCheckDone = false;
static int8_t s_lastRssi = 0;

const char* getGaiaStatusString(uint8_t status) {
    switch (status) {
        case 0: return "SUCCESS";
        case 1: return "NOT SUPPORTED";
        case 2: return "NOT AUTHENTICATED";
        case 3: return "INSUFFICIENT RESOURCES";
        case 4: return "AUTHENTICATING";
        case 5: return "INVALID PARAMETER";
        case 6: return "INCORRECT STATE";
        case 7: return "IN PROGRESS";
        default: return "UNKNOWN STATUS";
    }
}

const char* getFilterName(uint16_t ordinal) {
    switch (ordinal) {
        case 0: return "BYPASS";
        case 1: return "LOW_PASS_1";
        case 2: return "HIGH_PASS_1";
        case 3: return "ALL_PASS_1";
        case 4: return "LOW_SHELF_1";
        case 5: return "HIGH_SHELF_1";
        case 6: return "TILT_1";
        case 7: return "LOW_PASS_2";
        case 8: return "HIGH_PASS_2";
        case 9: return "ALL_PASS_2";
        case 10: return "LOW_SHELF_2";
        case 11: return "HIGH_SHELF_2";
        case 12: return "TILT_2";
        case 13: return "PARAMETRIC_EQUALIZER";
        default: return "UNKNOWN_FILTER";
    }
}

const char* getTwsSpeakerName(uint8_t speaker) {
    return (speaker == 0) ? "MASTER_SPEAKER" : "SLAVE_SPEAKER";
}

const char* getTwsChannelName(uint8_t channel) {
    switch (channel) {
        case 0: return "STEREO";
        case 1: return "LEFT";
        case 2: return "RIGHT";
        case 3: return "MONO";
        default: return "UNKNOWN";
    }
}

const char* getCodecName(uint8_t codec) {
    switch (codec) {
        case 0: return "SBC";
        case 1: return "aptX";
        case 2: return "AAC";
        case 3: return "aptX-LL (Low Latency)";
        case 4: return "aptX-HL";
        case 5: return "aptX-HD";
        case 6: return "LDAC";
        case 7: return "aptX Adaptive";
        default: return "UNKNOWN CODEC";
    }
}

const char* getAudioSourceName(uint8_t src) {
    switch (src) {
        case 0: return "NONE / OFF";
        case 1: return "Bluetooth Audio";
        case 2: return "AUX / Line-In";
        case 3: return "USB Audio";
        case 4: return "FM Radio";
        case 5: return "SPDIF / Optical";
        default: return "UNKNOWN SOURCE";
    }
}

const char* getGaiaEventName(uint8_t eventId) {
    switch (eventId) {
        case 1: return "RSSI_LOW_THRESHOLD";
        case 2: return "RSSI_HIGH_THRESHOLD";
        case 3: return "BATTERY_LOW_THRESHOLD";
        case 4: return "BATTERY_HIGH_THRESHOLD";
        case 5: return "DEVICE_STATE_CHANGED";
        case 6: return "PIO_CHANGED";
        case 7: return "DEBUG_MESSAGE";
        case 8: return "BATTERY_CHARGED";
        case 9: return "CHARGER_CONNECTION";
        case 10: return "CAPSENSE_UPDATE";
        case 11: return "USER_ACTION";
        case 12: return "SPEECH_RECOGNITION";
        case 13: return "AV_COMMAND";
        case 14: return "REMOTE_BATTERY_LEVEL";
        case 15: return "KEY";
        case 16: return "DFU_STATE";
        case 17: return "UART_RECEIVED_DATA";
        case 18: return "VMU_PACKET";
        case 19: return "HOST_NOTIFICATION";
        default: return "UNKNOWN_EVENT";
    }
}

extern bool powerBtMode();
static uint32_t s_auxRetryCooldownUntil = 0;

static void handleConnectionFailure() {
    s_isConnected = false;
    s_sppHandle = 0;
    s_targetFound = false;
    s_connFailures++;

    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "GAIA SPP connection failed. (Attempt %d/3)", s_connFailures);
    commsLog("warn", errBuf);

    if (stateSelfTestIsBtStepActive()) {
        s_connState = GAIA_CONN_IDLE;
        s_lastScanStartMs = 0;
        return;
    }

    if (s_connFailures >= 3) {
        s_connFailures = 0;
        if (s_powerCyclesCount < 1) {
            s_powerCyclesCount++;
            if (powerBtMode()) {
                char cycleBuf[128];
                snprintf(cycleBuf, sizeof(cycleBuf), "GAIA SPP connection failed 3 times in BT mode. Hard power cycling QCC module (Cycle %d/1)...", s_powerCyclesCount);
                commsLog("error", cycleBuf);
                s_connState = GAIA_CONN_IDLE;
                powerHardCycleBt();
            } else {
                commsLog("warn", "GAIA SPP connection failed 3 times in AUX mode. Cooling down 30s before retrying scan...");
                s_connState = GAIA_CONN_IDLE;
                s_auxRetryCooldownUntil = (uint32_t)(esp_timer_get_time() / 1000ULL) + 30000;
            }
        } else {
            commsLog("error", "GAIA connection failed permanently. Disabling GAIA commands but keeping system ON.");
            s_powerCyclesCount = 0;
            s_connState = GAIA_CONN_IDLE;
            powerSetBtFailedPerm(true);
        }
    } else {
        s_connState = GAIA_CONN_IDLE;
        commsLog("info", "Retrying Bluetooth scan and SPP discovery...");
    }
}

static void strTrim(std::string &s) {
    size_t p = s.find_first_not_of(" \t\n\r");
    if (p == std::string::npos) { s.clear(); return; }
    size_t q = s.find_last_not_of(" \t\n\r");
    s = s.substr(p, q - p + 1);
}

static void strToLower(std::string &s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
}

static bool checkDiscoveredDevice(esp_bt_gap_cb_param_t::disc_res_param *res) {
    if (memcmp(res->bda, targetAddress, 6) == 0) {
        return true;
    }
    for (int i = 0; i < res->num_prop; i++) {
        if (res->prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            char name[64] = {0};
            size_t len = res->prop[i].len < 63 ? res->prop[i].len : 63;
            memcpy(name, res->prop[i].val, len);
            if (strstr(name, targetName) != NULL) {
                return true;
            }
        } else if (res->prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t *eir = (uint8_t*)res->prop[i].val;
            uint8_t eir_len = res->prop[i].len;
            uint8_t *p = eir;
            while (p < eir + eir_len) {
                uint8_t length = *p++;
                if (length == 0) break;
                uint8_t type = *p++;
                if (type == 0x08 || type == 0x09) {
                    char name[64] = {0};
                    size_t name_len = (length - 1) < 63 ? (length - 1) : 63;
                    memcpy(name, p, name_len);
                    if (strstr(name, targetName) != NULL) {
                        return true;
                    }
                }
                p += (length - 1);
            }
        }
    }
    return false;
}

static void espGapCb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            if (s_connState == GAIA_CONN_SCANNING && !s_targetFound && checkDiscoveredDevice(&param->disc_res)) {
                s_targetFound = true;
                memcpy(targetAddress, param->disc_res.bda, 6);
                char discMsg[128];
                snprintf(discMsg, sizeof(discMsg), "Discovered 'Jacktor Audio' (%02X:%02X:%02X:%02X:%02X:%02X) via Bluetooth Scan!",
                         targetAddress[0], targetAddress[1], targetAddress[2], targetAddress[3], targetAddress[4], targetAddress[5]);
                commsLog("info", discMsg);
                esp_bt_gap_cancel_discovery();
                s_connState = GAIA_CONN_SDP_DISCOVERY;
                commsLog("info", "Starting SPP SDP Discovery...");
                esp_spp_start_discovery(targetAddress);
            }
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                if (s_connState == GAIA_CONN_SCANNING && !s_targetFound) {
                    commsLog("warn", "Bluetooth inquiry discovery stopped without finding target device.");
                    handleConnectionFailure();
                }
            }
            break;
        default:
            break;
    }
}

static uint8_t calculateGaiaChecksum(const uint8_t* buffer, size_t length) {
    uint8_t checksum = 0x00;
    for (size_t i = 0; i < length; ++i) {
        checksum ^= buffer[i];
    }
    return checksum;
}

static size_t buildGaiaFrame(uint16_t commandId, const uint8_t* payload, uint8_t payloadLen, uint8_t* txBuffer) {
    const uint8_t flags = GAIA_FLAG_CHECKSUM;

    txBuffer[OFFSET_SOF]         = GAIA_SOF_BYTE;
    txBuffer[OFFSET_VERSION]     = GAIA_PROTOCOL_VERSION;
    txBuffer[OFFSET_FLAGS]       = flags;
    txBuffer[OFFSET_LENGTH]      = payloadLen;
    txBuffer[OFFSET_VENDOR_MSB]  = static_cast<uint8_t>((GAIA_VENDOR_QUALCOMM >> 8) & 0xFF);
    txBuffer[OFFSET_VENDOR_LSB]  = static_cast<uint8_t>(GAIA_VENDOR_QUALCOMM & 0xFF);
    txBuffer[OFFSET_COMMAND_MSB] = static_cast<uint8_t>((commandId >> 8) & 0xFF);
    txBuffer[OFFSET_COMMAND_LSB] = static_cast<uint8_t>(commandId & 0xFF);

    if (payload != nullptr && payloadLen > 0) {
        for (uint8_t i = 0; i < payloadLen; ++i) {
            txBuffer[GAIA_HEADER_SIZE + i] = payload[i];
        }
    }

    size_t frameSize = GAIA_HEADER_SIZE + payloadLen;

    if (flags & GAIA_FLAG_CHECKSUM) {
        txBuffer[frameSize] = calculateGaiaChecksum(txBuffer, frameSize);
        frameSize++;
    }

    return frameSize;
}

static bool parseGaiaFrame(const uint8_t* buffer, size_t length, GaiaPacket& packet) {
    if (buffer == nullptr || length < GAIA_HEADER_SIZE) {
        return false;
    }

    if (buffer[OFFSET_SOF] != GAIA_SOF_BYTE) {
        return false;
    }

    const uint8_t version = buffer[OFFSET_VERSION];
    const uint8_t flags = buffer[OFFSET_FLAGS];
    const uint8_t payloadLength = buffer[OFFSET_LENGTH];
    const bool hasChecksum = (flags & GAIA_FLAG_CHECKSUM) != 0;

    const size_t expectedTotalSize = GAIA_HEADER_SIZE + payloadLength + (hasChecksum ? 1 : 0);
    if (length < expectedTotalSize) {
        return false;
    }

    if (hasChecksum) {
        uint8_t calculatedChk = calculateGaiaChecksum(buffer, GAIA_HEADER_SIZE + payloadLength);
        uint8_t frameChk = buffer[GAIA_HEADER_SIZE + payloadLength];
        if (calculatedChk != frameChk) {
            return false;
        }
    }

    packet.version = version;
    packet.enableChecksum = hasChecksum;
    packet.vendorId = (static_cast<uint16_t>(buffer[OFFSET_VENDOR_MSB]) << 8) | buffer[OFFSET_VENDOR_LSB];
    packet.commandId = (static_cast<uint16_t>(buffer[OFFSET_COMMAND_MSB]) << 8) | buffer[OFFSET_COMMAND_LSB];

    packet.payload.clear();
    if (payloadLength > 0) {
        packet.payload.assign(buffer + GAIA_HEADER_SIZE, buffer + GAIA_HEADER_SIZE + payloadLength);
    }

    return true;
}

static void decodePSKeyString(const uint8_t* data, size_t data_len, char* out_str) {
    size_t len = data_len;
    if (len % 2 == 1) len++;

    uint8_t* temp = new uint8_t[len];
    memset(temp, 0, len);

    for (size_t i = 0; i < data_len; i += 2) {
        if (i + 1 >= data_len) {
            temp[i + 1] = data[i];
            temp[i] = 0;
        } else {
            temp[i + 1] = data[i];
            temp[i] = data[i + 1];
        }
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < len; ++i) {
        if (temp[i] != 0) {
            out_str[out_idx++] = static_cast<char>(temp[i]);
        }
    }
    out_str[out_idx] = '\0';

    delete[] temp;
}

static void decodeGaiaEvent(uint8_t eventId, const uint8_t* payload, size_t len) {
    char buf[128];
    snprintf(buf, sizeof(buf), "GAIA Event ID: 0x%02X (%s)", eventId, getGaiaEventName(eventId));
    commsLog("info", buf);

    if (len < 2) return;
    uint8_t val = payload[1];

    switch (eventId) {
        case 3:
            commsLog("info", "  Desc: Warning: Battery Low (Low Threshold)!");
            break;
        case 4:
            commsLog("info", "  Desc: Warning: Battery High (High Threshold)!");
            break;
        case 5: {
            const char* st = "UNKNOWN";
            switch (val) {
                case 0: st = "DISCONNECTED / POWER OFF"; break;
                case 1: st = "PAIRING / CONNECTABLE"; break;
                case 2: st = "DISCOVERABLE"; break;
                case 3: st = "CONNECTED"; break;
                case 4: st = "STREAMING AUDIO (A2DP)"; break;
            }
            snprintf(buf, sizeof(buf), "  Desc: Device Status -> %s", st);
            commsLog("info", buf);
            break;
        }
        case 6:
            snprintf(buf, sizeof(buf), "  Desc: PIO pin changed. State (Bitmask): 0x%02X", val);
            commsLog("info", buf);
            break;
        case 8:
            commsLog("info", "  Desc: Battery Charging Complete (Fully Charged)!");
            break;
        case 9:
            snprintf(buf, sizeof(buf), "  Desc: Charger Connection -> %s", val == 1 ? "CONNECTED (CHARGING)" : "DISCONNECTED");
            commsLog("info", buf);
            break;
        case 13:
            snprintf(buf, sizeof(buf), "  Desc: Received AV/AVRCP OpCode: 0x%02X", val);
            commsLog("info", buf);
            break;
        case 14:
            snprintf(buf, sizeof(buf), "  Desc: Remote Device Battery Level: %d%%", val);
            commsLog("info", buf);
            break;
        default:
            snprintf(buf, sizeof(buf), "  Desc: Raw Event Data Byte1=0x%02X", val);
            commsLog("info", buf);
            break;
    }
}

static void handleIncomingGaiaPacket(const uint8_t* buffer, size_t length) {
    GaiaPacket rxPacket;
    if (parseGaiaFrame(buffer, length, rxPacket)) {
        if (rxPacket.payload.empty()) {
            return;
        }

        uint8_t status = rxPacket.payload[0];
        bool is_success = (status == 0x00);

        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "GAIA RX: Cmd=0x%04X, Status=0x%02X (%s)",
                 rxPacket.commandId, status, getGaiaStatusString(status));
        commsLog("info", logMsg);

        switch (rxPacket.commandId) {
            case (CMD_DEVICE_RESET | 0x8000):
                commsLog("info", is_success ? "ACK: QCC5125 module rebooting." : "ACK: Reboot command failed or unsupported.");
                break;

            case (CMD_FACTORY_DEFAULT_RESET | 0x8000):
                commsLog("info", is_success ? "ACK: QCC5125 module factory reset successful." : "ACK: Factory reset failed.");
                break;

            case (CMD_GET_API_VERSION | 0x8000):
                if (is_success) {
                    char buf[64];
                    if (rxPacket.payload.size() >= 3) {
                        uint16_t version = (rxPacket.payload[1] << 8) | rxPacket.payload[2];
                        snprintf(buf, sizeof(buf), "GAIA API Version: %d (0x%04X)", version, version);
                    } else if (rxPacket.payload.size() == 2) {
                        snprintf(buf, sizeof(buf), "GAIA API Version: %d (0x%02X)", rxPacket.payload[1], rxPacket.payload[1]);
                    } else {
                        snprintf(buf, sizeof(buf), "GAIA API Version: OK");
                    }
                    commsLog("info", buf);
                } else {
                    commsLog("warn", "Failed to query GAIA API Version.");
                }
                break;

            case (CMD_GET_APPLICATION_VERSION | 0x8000):
                if (is_success && rxPacket.payload.size() > 1) {
                    std::string verStr(rxPacket.payload.begin() + 1, rxPacket.payload.end());
                    char buf[128];
                    snprintf(buf, sizeof(buf), "QCC5125 Application Version: %s", verStr.c_str());
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Application Version.");
                }
                break;

            case (CMD_GET_CURRENT_RSSI | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    int8_t rssiVal = static_cast<int8_t>(rxPacket.payload[1]);
                    s_lastRssi = rssiVal;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "RSSI Signal Strength: %d dBm", rssiVal);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query RSSI.");
                }
                break;

            case (CMD_GET_CURRENT_BATTERY_LEVEL | 0x8000):
                if (is_success) {
                    char buf[64];
                    if (rxPacket.payload.size() >= 3) {
                        uint16_t batLevel = (rxPacket.payload[1] << 8) | rxPacket.payload[2];
                        snprintf(buf, sizeof(buf), "Battery Level: %u%%", batLevel);
                    } else if (rxPacket.payload.size() == 2) {
                        snprintf(buf, sizeof(buf), "Battery Level: %u%%", rxPacket.payload[1]);
                    } else {
                        snprintf(buf, sizeof(buf), "Battery Level: OK");
                    }
                    commsLog("info", buf);
                } else {
                    commsLog("warn", "Failed to query Battery Level.");
                }
                break;

            case (CMD_GET_VOLUME | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Active Device Volume: %d", rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Active Device Volume.");
                }
                break;

            case (CMD_CHANGE_VOLUME | 0x8000):
                commsLog("info", is_success ? "ACK: Volume changed successfully." : "ACK: Failed to change volume.");
                break;

            case (CMD_GET_EQ_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Active EQ Preset: %d (0 = Bypass/Flat, 1-6 = Preset EQ)", rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query EQ Preset.");
                }
                break;

            case (CMD_SET_EQ_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: EQ Preset changed successfully." : "ACK: Failed to change EQ Preset.");
                break;

            case (CMD_GET_BASS_BOOST_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Bass Boost Status: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Bass Boost status.");
                }
                break;

            case (CMD_SET_BASS_BOOST_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: Bass Boost status updated successfully." : "ACK: Failed to update Bass Boost status.");
                break;

            case (CMD_GET_3D_ENHANCEMENT_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "3D Enhancement Status: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query 3D Enhancement status.");
                }
                break;

            case (CMD_SET_3D_ENHANCEMENT_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: 3D Enhancement status updated successfully." : "ACK: Failed to update 3D Enhancement status.");
                break;

            case (CMD_GET_USER_EQ_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "User EQ Status: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query User EQ status.");
                }
                break;

            case (CMD_SET_USER_EQ_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: User EQ status updated successfully." : "ACK: Failed to update User EQ status.");
                break;

            case (CMD_GET_EQ_PARAMETER | 0x8000):
                if (is_success && rxPacket.payload.size() >= 5) {
                    uint8_t band = (rxPacket.payload[2] & 0xF0) >> 4;
                    uint8_t param = rxPacket.payload[2] & 0x0F;
                    int16_t rawVal = static_cast<int16_t>((rxPacket.payload[3] << 8) | rxPacket.payload[4]);
                    char buf[128];

                    if (band == 0 && param == 1) {
                        snprintf(buf, sizeof(buf), "EQ Band %d MASTER_GAIN: Raw=%d, Decoded=%.2f dB", band, rawVal, static_cast<double>(rawVal) / 60.0);
                        commsLog("info", buf);
                    } else {
                        switch (param) {
                            case 0: {
                                uint16_t uRawVal = static_cast<uint16_t>(rawVal);
                                snprintf(buf, sizeof(buf), "EQ Band %d FILTER: Raw=%u, Decoded=%s", band, uRawVal, getFilterName(uRawVal));
                                commsLog("info", buf);
                                break;
                            }
                            case 1: {
                                uint16_t uRawVal = static_cast<uint16_t>(rawVal);
                                snprintf(buf, sizeof(buf), "EQ Band %d FREQUENCY: Raw=%u, Decoded=%.2f Hz", band, uRawVal, static_cast<double>(uRawVal) / 3.0);
                                commsLog("info", buf);
                                break;
                            }
                            case 2: {
                                snprintf(buf, sizeof(buf), "EQ Band %d GAIN: Raw=%d, Decoded=%.2f dB", band, rawVal, static_cast<double>(rawVal) / 60.0);
                                commsLog("info", buf);
                                break;
                            }
                            case 3: {
                                uint16_t uRawVal = static_cast<uint16_t>(rawVal);
                                snprintf(buf, sizeof(buf), "EQ Band %d QUALITY: Raw=%u, Decoded=%.4f", band, uRawVal, static_cast<double>(uRawVal) / 4096.0);
                                commsLog("info", buf);
                                break;
                            }
                            default:
                                snprintf(buf, sizeof(buf), "EQ Band %d Param %d: Raw=%d", band, param, rawVal);
                                commsLog("info", buf);
                                break;
                        }
                    }
                } else if (!is_success) {
                    commsLog("warn", "Failed to query custom EQ parameter.");
                }
                break;

            case (CMD_SET_EQ_PARAMETER | 0x8000):
                commsLog("info", is_success ? "ACK: EQ parameter saved successfully." : "ACK: Failed to change EQ parameter.");
                break;

            case (CMD_FIND_MY_REMOTE | 0x8000):
                commsLog("info", is_success ? "ACK: Find My Remote alert triggered." : "ACK: Failed to trigger Find My Remote.");
                break;

            case (CMD_GET_TWS_AUDIO_ROUTING | 0x8000):
                if (is_success && rxPacket.payload.size() >= 3) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "TWS Speaker: %s | Audio Routing: %s", getTwsSpeakerName(rxPacket.payload[1]), getTwsChannelName(rxPacket.payload[2]));
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query TWS audio routing.");
                }
                break;

            case (CMD_SET_TWS_AUDIO_ROUTING | 0x8000):
                commsLog("info", is_success ? "ACK: TWS audio routing updated." : "ACK: Failed to update TWS audio routing.");
                break;

            case (CMD_GET_TWS_VOLUME | 0x8000):
                if (is_success && rxPacket.payload.size() >= 3) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "TWS Speaker: %s | Volume: %d (0-127)", getTwsSpeakerName(rxPacket.payload[1]), rxPacket.payload[2]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query TWS volume.");
                }
                break;

            case (CMD_SET_TWS_VOLUME | 0x8000):
                commsLog("info", is_success ? "ACK: TWS volume updated." : "ACK: Failed to update TWS volume.");
                break;

            case (CMD_RETRIEVE_PS_KEY | 0x8000):
                if (is_success && rxPacket.payload.size() >= 3) {
                    uint16_t key = (rxPacket.payload[1] << 8) | rxPacket.payload[2];
                    size_t data_len = rxPacket.payload.size() - 3;
                    char* decoded = new char[data_len + 2];
                    memset(decoded, 0, data_len + 2);

                    if (data_len > 0) {
                        decodePSKeyString(&rxPacket.payload[3], data_len, decoded);
                    }

                    char outBuf[128];
                    if (key == 0x00C8) {
                        snprintf(outBuf, sizeof(outBuf), "Bluetooth Name: %s", decoded);
                    } else if (key == 0x00FA) {
                        snprintf(outBuf, sizeof(outBuf), "Bluetooth PIN: %s", decoded);
                    } else {
                        snprintf(outBuf, sizeof(outBuf), "PS Key 0x%04X Data: %s", key, decoded);
                    }
                    commsLog("info", outBuf);
                    delete[] decoded;
                } else if (!is_success) {
                    commsLog("warn", "Failed to read PS Key data.");
                }
                break;

            case (CMD_STORE_PS_KEY | 0x8000):
                commsLog("info", is_success ? "ACK: PS Key configuration saved." : "ACK: Failed to save PS Key configuration.");
                break;

            case (CMD_AV_REMOTE_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: Media control command (AVRCP) executed." : "ACK: Failed to execute media control command (AVRCP).");
                break;

            case (CMD_GET_LED_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "LED Status: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query LED status.");
                }
                break;

            case (CMD_SET_LED_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: LED status updated." : "ACK: Failed to update LED status.");
                break;

            case (CMD_PLAY_TONE | 0x8000):
                commsLog("info", is_success ? "ACK: Tone playback triggered." : "ACK: Failed to trigger tone playback.");
                break;

            case (CMD_REGISTER_NOTIFICATION | 0x8000):
                commsLog("info", is_success ? "ACK: Notification registered successfully." : "ACK: Failed to register notification.");
                break;

            case (CMD_CANCEL_NOTIFICATION | 0x8000):
                commsLog("info", is_success ? "ACK: Notification cancelled successfully." : "ACK: Failed to cancel notification.");
                break;

            case (CMD_GET_DATA_ENDPOINT_MODE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Data Endpoint Mode: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Data Endpoint Mode.");
                }
                break;

            case (CMD_SET_DATA_ENDPOINT_MODE | 0x8000):
                commsLog("info", is_success ? "ACK: Data Endpoint Mode updated." : "ACK: Failed to update Data Endpoint Mode.");
                break;

            case (CMD_GET_DEVICE_NAME | 0x8000):
                if (is_success && rxPacket.payload.size() > 1) {
                    std::string devName(rxPacket.payload.begin() + 1, rxPacket.payload.end());
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Device Name (Active): %s", devName.c_str());
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Device Name.");
                }
                break;

            case (CMD_SET_DEVICE_NAME | 0x8000):
                commsLog("info", is_success ? "ACK: Device name updated successfully." : "ACK: Failed to update device name.");
                break;

            case (CMD_GET_BOOT_MODE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Device Boot Mode: 0x%02X", rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Boot Mode.");
                }
                break;

            case (CMD_GET_MODULE_ID | 0x8000):
                if (is_success) {
                    char buf[64];
                    if (rxPacket.payload.size() >= 3) {
                        uint16_t modId = (rxPacket.payload[1] << 8) | rxPacket.payload[2];
                        snprintf(buf, sizeof(buf), "Qualcomm Module/Chip ID: 0x%04X (%d)", modId, modId);
                    } else if (rxPacket.payload.size() == 2) {
                        snprintf(buf, sizeof(buf), "Qualcomm Module/Chip ID: 0x%02X (%d)", rxPacket.payload[1], rxPacket.payload[1]);
                    } else {
                        snprintf(buf, sizeof(buf), "Qualcomm Module/Chip ID: OK");
                    }
                    commsLog("info", buf);
                } else {
                    commsLog("warn", "Failed to query Module ID.");
                }
                break;

            case (CMD_GET_PEER_ADDRESS | 0x8000):
                if (is_success && rxPacket.payload.size() >= 7) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Peer Bluetooth Address: %02X:%02X:%02X:%02X:%02X:%02X",
                             rxPacket.payload[1], rxPacket.payload[2], rxPacket.payload[3],
                             rxPacket.payload[4], rxPacket.payload[5], rxPacket.payload[6]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Peer Bluetooth Address.");
                }
                break;

            case (CMD_GET_CODEC | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Active Audio Codec: %s (0x%02X)", getCodecName(rxPacket.payload[1]), rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Active Audio Codec.");
                }
                break;

            case (CMD_SET_CODEC | 0x8000):
                commsLog("info", is_success ? "ACK: Audio codec updated." : "ACK: Failed to update audio codec.");
                break;

            case (CMD_GET_POWER_STATE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Power State: %s", rxPacket.payload[1] ? "ON" : "OFF");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Power State.");
                }
                break;

            case (CMD_SET_POWER_STATE | 0x8000):
                commsLog("info", is_success ? "ACK: Power state updated." : "ACK: Failed to update power state.");
                break;

            case (CMD_GET_PIO_STATE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    uint32_t pioMask = 0;
                    for (size_t i = 1; i < rxPacket.payload.size() && i <= 4; ++i) {
                        pioMask = (pioMask << 8) | rxPacket.payload[i];
                    }
                    char buf[64];
                    snprintf(buf, sizeof(buf), "PIO Pin Status (Bitmask): 0x%08X", (unsigned int)pioMask);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query PIO Pin Status.");
                }
                break;

            case (CMD_SET_PIO_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: PIO pin configuration saved." : "ACK: Failed to update PIO pins.");
                break;

            case (CMD_GET_AUDIO_SOURCE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Active Audio Source: %s (0x%02X)", getAudioSourceName(rxPacket.payload[1]), rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Audio Source.");
                }
                break;

            case (CMD_SET_AUDIO_SOURCE | 0x8000):
                commsLog("info", is_success ? "ACK: Audio source updated." : "ACK: Failed to update audio source.");
                break;

            case (CMD_ENTER_BLUETOOTH_PAIRING_MODE | 0x8000):
                commsLog("info", is_success ? "ACK: QCC5125 entered Bluetooth pairing mode." : "ACK: Failed to enter Bluetooth pairing mode.");
                break;

            case (CMD_DELETE_PDL | 0x8000):
                commsLog("info", is_success ? "ACK: Paired Device List (PDL) erased." : "ACK: Failed to erase Paired Device List (PDL).");
                break;

            case (CMD_GET_AUDIO_PROMPT_LANGUAGE | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Voice Prompt Language: ID %d", rxPacket.payload[1]);
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Voice Prompt language.");
                }
                break;

            case (CMD_CHANGE_AUDIO_PROMPT_LANGUAGE | 0x8000):
                commsLog("info", is_success ? "ACK: Voice prompt language updated." : "ACK: Failed to update voice prompt language.");
                break;

            case (CMD_GET_SPEECH_RECOGNITION_CONTROL | 0x8000):
                if (is_success && rxPacket.payload.size() >= 2) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Speech Recognition Status: %s", rxPacket.payload[1] ? "ACTIVE (ON)" : "INACTIVE (OFF)");
                    commsLog("info", buf);
                } else if (!is_success) {
                    commsLog("warn", "Failed to query Speech Recognition status.");
                }
                break;

            case (CMD_SET_SPEECH_RECOGNITION_CONTROL | 0x8000):
                commsLog("info", is_success ? "ACK: Speech Recognition status updated." : "ACK: Failed to update Speech Recognition status.");
                break;

            case CMD_EVENT_NOTIFICATION:
                commsLog("info", "Notification: Event notification received from QCC5125.");
                if (rxPacket.payload.size() >= 1) {
                    decodeGaiaEvent(rxPacket.payload[0], &rxPacket.payload[0], rxPacket.payload.size());
                }
                break;

            default:
                break;
        }
    }
}

static void espSppCb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch (event) {
        case ESP_SPP_INIT_EVT:
            break;
        case ESP_SPP_DISCOVERY_COMP_EVT:
            if (s_connState == GAIA_CONN_SDP_DISCOVERY) {
                uint8_t scn = 1;
                if (param->disc_comp.status == ESP_SPP_SUCCESS) {
                    if (param->disc_comp.scn_num > 0) {
                        scn = param->disc_comp.scn[0];
                    }
                    char scnMsg[64];
                    snprintf(scnMsg, sizeof(scnMsg), "SDP discovery complete. SPP Channel SCN = %d", scn);
                    commsLog("info", scnMsg);
                    s_connState = GAIA_CONN_CONNECTING;
                    commsLog("info", "Initiating SPP connection...");
                    esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER, scn, targetAddress);
                } else {
                    commsLog("warn", "SDP discovery failed");
                    handleConnectionFailure();
                }
            }
            break;
        case ESP_SPP_OPEN_EVT:
            if (param->open.status == ESP_SPP_SUCCESS) {
                s_sppHandle = param->open.handle;
                s_isConnected = true;
                s_connState = GAIA_CONN_CONNECTED;
                s_connFailures = 0;
                s_powerCyclesCount = 0;
                commsLog("info", "Successfully connected to Jacktor Audio QCC Module via SPP!");
                powerNotifyBtConnected();
            } else {
                commsLog("warn", "SPP Open Failed");
                handleConnectionFailure();
            }
            break;
        case ESP_SPP_CLOSE_EVT:
            s_isConnected = false;
            s_sppHandle = 0;
            s_connState = GAIA_CONN_IDLE;
            powerClearBtFailedPerm();
            if (param->close.status != ESP_SPP_SUCCESS) {
                char closeErr[64];
                snprintf(closeErr, sizeof(closeErr), "SPP Connection closed (status: 0x%02X)", param->close.status);
                commsLog("warn", closeErr);
            } else {
                commsLog("info", "SPP Connection closed");
            }
            break;
        case ESP_SPP_DATA_IND_EVT:
            if (param->data_ind.len > 0) {
                handleIncomingGaiaPacket(param->data_ind.data, param->data_ind.len);
            }
            break;
        default:
            break;
    }
}

void gaiaSendCommand(uint16_t commandId, const uint8_t* payload, uint8_t payloadLen) {
    if (!s_isConnected || s_sppHandle == 0) {
        debugSerial.println("ERR: GAIA command failed - Bluetooth SPP not connected");
        commsLog("error", "GAIA command rejected: Bluetooth SPP not connected");
        return;
    }

    uint8_t txBuffer[260];
    size_t packetSize = buildGaiaFrame(commandId, payload, payloadLen, txBuffer);
    esp_spp_write(s_sppHandle, packetSize, txBuffer);
    char logBuf[64];
    snprintf(logBuf, sizeof(logBuf), "GAIA TX: 0x%04X", commandId);
    commsLog("info", logBuf);
}

void gaiaSendAvrcpControl(AvrcpOpCode opCode) {
    uint8_t avrcpPayload[1] = { static_cast<uint8_t>(opCode) };
    gaiaSendCommand(CMD_AV_REMOTE_CONTROL, avrcpPayload, 1);
}

static void setBluetoothPSKey(uint16_t key, const std::string &str) {
    size_t str_len = str.length();
    size_t L = str_len;
    if (L % 2 == 1) {
        L++;
    }

    size_t PAYLOAD_LENGTH = (L * 2) + 2;
    uint8_t* payload = new uint8_t[PAYLOAD_LENGTH];
    memset(payload, 0, PAYLOAD_LENGTH);

    payload[0] = static_cast<uint8_t>((key >> 8) & 0xFF);
    payload[1] = static_cast<uint8_t>(key & 0xFF);

    uint8_t* arry = new uint8_t[L];
    memset(arry, 0, L);
    memcpy(arry, str.c_str(), str_len);

    for (size_t i = 0; i < L; i += 2) {
        payload[2 + i + 1] = arry[i];
        payload[2 + i] = arry[i + 1];
    }

    gaiaSendCommand(CMD_STORE_PS_KEY, payload, PAYLOAD_LENGTH);

    delete[] arry;
    delete[] payload;
}

void gaiaInit() {
    (void)targetName;
    esp_log_level_set("BT_HCI", ESP_LOG_ERROR);
    esp_log_level_set("BT_RFCOMM", ESP_LOG_ERROR);
    esp_log_level_set("BT_APPL", ESP_LOG_ERROR);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) == ESP_OK) {
        esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
        esp_bluedroid_init();
        esp_bluedroid_enable();
        esp_bt_gap_set_device_name("ESP32_GAIA_Controller");
        esp_bt_gap_register_callback(espGapCb);
        esp_spp_register_callback(espSppCb);

        esp_spp_cfg_t spp_cfg = {
            .mode = ESP_SPP_MODE_CB,
            .enable_l2cap_ertm = false,
            .tx_buffer_size = 0,
        };
        esp_spp_enhanced_init(&spp_cfg);

        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
        esp_bt_pin_code_t pin_code = {'2', '0', '2', '6'};
        esp_err_t err = esp_bt_gap_set_pin(pin_type, 4, pin_code);
        (void)err;

        // Very close distance (~1 cm); reduce ESP32 TX power to minimum (-12 dBm) to minimize RF noise / receiver clipping
        esp_bredr_tx_power_set(ESP_PWR_LVL_N12, ESP_PWR_LVL_N12);
    }
}

bool gaiaIsConnected() {
    return s_isConnected && s_sppHandle != 0;
}

int8_t gaiaGetLastRssi() {
    return s_lastRssi;
}

static bool s_initLogged = false;

void gaiaDisconnect() {
    if (s_connState == GAIA_CONN_SCANNING) {
        esp_bt_gap_cancel_discovery();
    }
    if (s_isConnected && s_sppHandle != 0) {
        esp_spp_disconnect(s_sppHandle);
    }
    s_connFailures = 0;
    s_powerCyclesCount = 0;
    s_isConnected = false;
    s_sppHandle = 0;
    s_connState = GAIA_CONN_IDLE;
    s_targetFound = false;
    s_initLogged = false;
    s_initialCheckDone = false;
    s_auxRetryCooldownUntil = 0;
    s_lastScanStartMs = 0;
}

void gaiaResetConnectionState() {
    if (s_connState == GAIA_CONN_SCANNING) {
        esp_bt_gap_cancel_discovery();
    }
    if (s_isConnected && s_sppHandle != 0) {
        esp_spp_disconnect(s_sppHandle);
    }
    s_connFailures = 0;
    s_powerCyclesCount = 0;
    s_isConnected = false;
    s_sppHandle = 0;
    s_connState = GAIA_CONN_IDLE;
    s_targetFound = false;
    s_auxRetryCooldownUntil = 0;
    s_lastScanStartMs = 0;
}

uint8_t gaiaGetFailureCount() {
    return s_connFailures;
}

void gaiaTick(uint32_t now) {
    if (!powerIsOn() || !powerBtEnabled() || powerBtFailedPerm()) {
        if (s_isConnected || s_connState != GAIA_CONN_IDLE) {
            gaiaDisconnect();
        }
        return;
    }

    if (s_auxRetryCooldownUntil > 0 && now < s_auxRetryCooldownUntil && !powerBtMode()) {
        return;
    }

    if (!s_initLogged) {
        s_initLogged = true;
        commsLog("info", "ESP SPP initialized successfully");
        commsLog("info", "GAIA Bluetooth client initialized with PIN 2026");
    }

    if (s_isConnected && s_connState == GAIA_CONN_CONNECTED) {
        return;
    }

    if (powerBtHardCycling()) {
        return;
    }

    SelfTestStatus stSt = stateSelfTestGetStatus();
    if (stSt == SelfTestStatus::RUNNING) {
        if (!stateSelfTestIsBtStepActive() || !stateSelfTestIsBtConnectAllowed()) {
            return;
        }
    } else {
        uint32_t powerOnMs = powerGetBtPowerOnMs();
        if (powerOnMs == 0) {
            return;
        }

        // Wait at least 2000ms after QCC power is applied for DSP & BT stack to initialize
        if (now < powerOnMs || now - powerOnMs < 2000) {
            return;
        }
    }

    static uint32_t btLowStartMs = 0;
    if (!powerHwReadBtReady()) {
        if (btLowStartMs == 0) {
            btLowStartMs = now;
        }
        if (now - btLowStartMs >= 2000) {
            if (s_connState != GAIA_CONN_IDLE) {
                commsLog("info", "QCC5125 Power Detect (GPIO 5) LOW - Bluetooth module inactive, maintaining AUX mode.");
                s_connState = GAIA_CONN_IDLE;
            }
        }
        return;
    } else {
        btLowStartMs = 0;
    }

    if (s_connState == GAIA_CONN_IDLE) {
        if (powerHwReadBtReady()) {
            uint32_t scanCooldown = stateSelfTestIsBtStepActive() ? 500 : 1000;
            if (s_lastScanStartMs == 0 || (now - s_lastScanStartMs >= scanCooldown)) {
                s_lastScanStartMs = now;
                s_scanStartMs = now;
                if (s_connFailures < 2) {
                    s_targetFound = true;
                    s_connState = GAIA_CONN_SDP_DISCOVERY;
                    commsLog("info", "QCC5125 ready, launching direct SPP discovery...");
                    esp_spp_start_discovery(targetAddress);
                } else {
                    s_targetFound = false;
                    s_connState = GAIA_CONN_SCANNING;
                    commsLog("info", "Scanning for Bluetooth device 'Jacktor Audio' (53:4A:52:FE:00:AF)...");
                    uint8_t inq_len = stateSelfTestIsBtStepActive() ? 4 : 8;
                    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, inq_len, 0);
                }
            }
        }
    }
    else if (s_connState == GAIA_CONN_SCANNING) {
        uint32_t scanTimeout = stateSelfTestIsBtStepActive() ? 6000 : 10000;
        if (!s_targetFound && (now - s_scanStartMs >= scanTimeout)) {
            commsLog("warn", "Bluetooth 'Jacktor Audio' (53:4A:52:FE:00:AF) not found within timeout!");
            esp_bt_gap_cancel_discovery();
            handleConnectionFailure();
        }
    }
    else if (s_connState == GAIA_CONN_SDP_DISCOVERY || s_connState == GAIA_CONN_CONNECTING) {
        uint32_t sdpTimeout = stateSelfTestIsBtStepActive() ? 6000 : 8000;
        if (now - s_scanStartMs >= sdpTimeout) {
            commsLog("warn", "Bluetooth SDP/SPP connection timed out.");
            handleConnectionFailure();
        }
    }
}

static bool isGaiaCommand(const std::string& cmd) {
    static const char* const gaia_cmds[] = {
        "play", "pause", "mute", "stop", "next", "prev", "volup", "voldown",
        "battery", "reboot", "factoryreset", "deletepdl", "rssi", "apiver", "appver",
        "getbtname", "getbtpin", "setbtname", "setbtpin", "getdevicename", "setdevicename",
        "getbootmode", "setbootmode", "getmoduleid", "getpeeraddress", "getpreset", "setpreset",
        "getbass", "setbass", "get3d", "set3d", "getusereq", "setusereq", "geteqparam", "seteqparam",
        "playtone", "getvolume", "changevolume", "getled", "setled", "regnotif", "cancelnotif",
        "findremote", "gettwschan", "settwschan", "gettwsvol", "settwsvol", "getcodec", "setcodec",
        "getpower", "setpower", "getpio", "setpio", "getaudiosrc", "setaudiosrc", "enterpairing",
        "getpromptlang", "setpromptlang", "getspeech", "setspeech", "getdataendpoint", "setdataendpoint"
    };
    for (const char* c : gaia_cmds) {
        if (cmd == c) return true;
    }
    return false;
}

static std::vector<std::string> tokenizeCommand(std::string cmd) {
    std::vector<std::string> tokens;
    strTrim(cmd);
    size_t start = 0;
    while (start < cmd.length()) {
        size_t spaceIndex = cmd.find(' ', start);
        if (spaceIndex == std::string::npos) {
            std::string token = cmd.substr(start);
            strTrim(token);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            break;
        }
        std::string token = cmd.substr(start, spaceIndex - start);
        strTrim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
        start = spaceIndex + 1;
    }
    return tokens;
}

void gaiaProcessConsoleCommand(std::string command) {
    std::vector<std::string> args = tokenizeCommand(command);
    if (args.empty()) return;

    std::string main_cmd = args[0];
    strToLower(main_cmd);

    if (!isGaiaCommand(main_cmd)) {
        debugSerial.printf("ERR: Unknown command '%s'. Type 'help' for available commands.\n", main_cmd.c_str());
        return;
    }

    if (main_cmd == "setpower") {
        if (args.size() < 2) return;
        int val = std::stoi(args[1]);
        uint8_t payload[1] = { static_cast<uint8_t>(val ? 1 : 0) };
        if (val == 0) {
            if (gaiaIsConnected()) gaiaSendCommand(CMD_SET_POWER_STATE, payload, 1);
            powerSetBtEnabled(false);
        } else {
            powerSetBtEnabled(true);
            if (gaiaIsConnected()) gaiaSendCommand(CMD_SET_POWER_STATE, payload, 1);
        }
        return;
    }

    if (!gaiaIsConnected()) {
        debugSerial.println("ERR: GAIA command failed - Bluetooth SPP not connected");
        commsLog("error", "GAIA command rejected: Bluetooth SPP not connected");
        return;
    }

    if (main_cmd == "play" || main_cmd == "pause" || main_cmd == "mute" || main_cmd == "stop" || main_cmd == "next" || main_cmd == "prev" || main_cmd == "volup" || main_cmd == "voldown") {
        if (main_cmd == "play") gaiaSendAvrcpControl(AVRCP_PLAY);
        else if (main_cmd == "pause") gaiaSendAvrcpControl(AVRCP_PAUSE);
        else if (main_cmd == "mute") gaiaSendAvrcpControl(AVRCP_MUTE);
        else if (main_cmd == "stop") gaiaSendAvrcpControl(AVRCP_STOP);
        else if (main_cmd == "next") gaiaSendAvrcpControl(AVRCP_FORWARD);
        else if (main_cmd == "prev") gaiaSendAvrcpControl(AVRCP_BACKWARD);
        else if (main_cmd == "volup") gaiaSendAvrcpControl(AVRCP_VOLUME_UP);
        else if (main_cmd == "voldown") gaiaSendAvrcpControl(AVRCP_VOLUME_DOWN);
    }
    else if (main_cmd == "battery") {
        gaiaSendCommand(CMD_GET_CURRENT_BATTERY_LEVEL);
    }
    else if (main_cmd == "reboot" || main_cmd == "factoryreset" || main_cmd == "deletepdl") {
        if (main_cmd == "reboot") gaiaSendCommand(CMD_DEVICE_RESET);
        else if (main_cmd == "factoryreset") gaiaSendCommand(CMD_FACTORY_DEFAULT_RESET);
        else gaiaSendCommand(CMD_DELETE_PDL);
    }
    else if (main_cmd == "rssi") {
        gaiaSendCommand(CMD_GET_CURRENT_RSSI);
    }
    else if (main_cmd == "apiver") {
        gaiaSendCommand(CMD_GET_API_VERSION);
    }
    else if (main_cmd == "appver") {
        gaiaSendCommand(CMD_GET_APPLICATION_VERSION);
    }
    else if (main_cmd == "getbtname" || main_cmd == "getbtpin" || main_cmd == "setbtname" || main_cmd == "setbtpin") {
        if (main_cmd == "getbtname") {
            uint8_t payload[2] = { 0x00, 0xC8 };
            gaiaSendCommand(CMD_RETRIEVE_PS_KEY, payload, 2);
        } else if (main_cmd == "getbtpin") {
            uint8_t payload[2] = { 0x00, 0xFA };
            gaiaSendCommand(CMD_RETRIEVE_PS_KEY, payload, 2);
        } else if (main_cmd == "setbtname") {
            if (args.size() < 2) return;
            std::string btname = "";
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) btname += " ";
                btname += args[i];
            }
            setBluetoothPSKey(0x00C8, btname);
        } else if (main_cmd == "setbtpin") {
            if (args.size() < 2) return;
            setBluetoothPSKey(0x00FA, args[1]);
        }
    }
    else if (main_cmd == "getdevicename" || main_cmd == "setdevicename" || main_cmd == "getbootmode" || main_cmd == "setbootmode" || main_cmd == "getmoduleid" || main_cmd == "getpeeraddress") {
        if (main_cmd == "getdevicename") {
            gaiaSendCommand(CMD_GET_DEVICE_NAME);
        } else if (main_cmd == "getbootmode") {
            gaiaSendCommand(CMD_GET_BOOT_MODE);
        } else if (main_cmd == "getmoduleid") {
            gaiaSendCommand(CMD_GET_MODULE_ID);
        } else if (main_cmd == "getpeeraddress") {
            gaiaSendCommand(CMD_GET_PEER_ADDRESS);
        } else if (main_cmd == "setdevicename") {
            if (args.size() < 2) return;
            std::string devname = "";
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) devname += " ";
                devname += args[i];
            }
            gaiaSendCommand(CMD_SET_DEVICE_NAME, (const uint8_t*)devname.c_str(), devname.length());
        } else if (main_cmd == "setbootmode") {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_DEVICE_RESET, payload, 1);
        }
    }
    else if (main_cmd == "getpreset" || main_cmd == "setpreset") {
        if (main_cmd == "getpreset") {
            gaiaSendCommand(CMD_GET_EQ_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            if (val < 0 || val > 6) return;
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_EQ_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "getbass" || main_cmd == "setbass") {
        if (main_cmd == "getbass") {
            gaiaSendCommand(CMD_GET_BASS_BOOST_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_BASS_BOOST_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "get3d" || main_cmd == "set3d") {
        if (main_cmd == "get3d") {
            gaiaSendCommand(CMD_GET_3D_ENHANCEMENT_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_3D_ENHANCEMENT_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "getusereq" || main_cmd == "setusereq") {
        if (main_cmd == "getusereq") {
            gaiaSendCommand(CMD_GET_USER_EQ_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_USER_EQ_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "geteqparam" || main_cmd == "seteqparam") {
        if (main_cmd == "geteqparam") {
            if (args.size() < 3) return;
            int band = std::stoi(args[1]);
            int param = std::stoi(args[2]);
            uint8_t payload[2] = { 1, static_cast<uint8_t>((band << 4) | param) };
            gaiaSendCommand(CMD_GET_EQ_PARAMETER, payload, 2);
        } else {
            if (args.size() < 4) return;
            int band = std::stoi(args[1]);
            int param = std::stoi(args[2]);
            int val_raw = std::stoi(args[3]);
            uint8_t payload[5] = {
                1,
                static_cast<uint8_t>((band << 4) | param),
                static_cast<uint8_t>((val_raw >> 8) & 0xFF),
                static_cast<uint8_t>(val_raw & 0xFF),
                1
            };
            gaiaSendCommand(CMD_SET_EQ_PARAMETER, payload, 5);
        }
    }
    else if (main_cmd == "playtone") {
        if (args.size() < 2) return;
        int val = std::stoi(args[1]);
        uint8_t payload[1] = { static_cast<uint8_t>(val) };
        gaiaSendCommand(CMD_PLAY_TONE, payload, 1);
    }
    else if (main_cmd == "getvolume" || main_cmd == "changevolume") {
        if (main_cmd == "getvolume") {
            gaiaSendCommand(CMD_GET_VOLUME);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_CHANGE_VOLUME, payload, 1);
        }
    }
    else if (main_cmd == "getled" || main_cmd == "setled") {
        if (main_cmd == "getled") {
            gaiaSendCommand(CMD_GET_LED_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val ? 1 : 0) };
            gaiaSendCommand(CMD_SET_LED_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "regnotif" || main_cmd == "cancelnotif") {
        if (args.size() < 2) return;
        int val = std::stoi(args[1]);
        uint8_t payload[1] = { static_cast<uint8_t>(val) };
        if (main_cmd == "regnotif") gaiaSendCommand(CMD_REGISTER_NOTIFICATION, payload, 1);
        else gaiaSendCommand(CMD_CANCEL_NOTIFICATION, payload, 1);
    }
    else if (main_cmd == "findremote") {
        if (args.size() < 2) return;
        int val = std::stoi(args[1]);
        uint8_t payload[1] = { static_cast<uint8_t>(val) };
        gaiaSendCommand(CMD_FIND_MY_REMOTE, payload, 1);
    }
    else if (main_cmd == "gettwschan" || main_cmd == "settwschan") {
        if (main_cmd == "gettwschan") {
            if (args.size() < 2) return;
            int speaker = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(speaker) };
            gaiaSendCommand(CMD_GET_TWS_AUDIO_ROUTING, payload, 1);
        } else {
            if (args.size() < 3) return;
            int speaker = std::stoi(args[1]);
            int channel = std::stoi(args[2]);
            uint8_t payload[2] = { static_cast<uint8_t>(speaker), static_cast<uint8_t>(channel) };
            gaiaSendCommand(CMD_SET_TWS_AUDIO_ROUTING, payload, 2);
        }
    }
    else if (main_cmd == "gettwsvol" || main_cmd == "settwsvol") {
        if (main_cmd == "gettwsvol") {
            if (args.size() < 2) return;
            int speaker = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(speaker) };
            gaiaSendCommand(CMD_GET_TWS_VOLUME, payload, 1);
        } else {
            if (args.size() < 3) return;
            int speaker = std::stoi(args[1]);
            int volume = std::stoi(args[2]);
            if (volume < 0) volume = 0;
            if (volume > 127) volume = 127;
            uint8_t payload[2] = { static_cast<uint8_t>(speaker), static_cast<uint8_t>(volume) };
            gaiaSendCommand(CMD_SET_TWS_VOLUME, payload, 2);
        }
    }
    else if (main_cmd == "getcodec" || main_cmd == "setcodec") {
        if (main_cmd == "getcodec") {
            gaiaSendCommand(CMD_GET_CODEC);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_CODEC, payload, 1);
        }
    }
    else if (main_cmd == "getpower" || main_cmd == "setpower") {
        if (main_cmd == "getpower") {
            gaiaSendCommand(CMD_GET_POWER_STATE);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val ? 1 : 0) };
            gaiaSendCommand(CMD_SET_POWER_STATE, payload, 1);
        }
    }
    else if (main_cmd == "getpio" || main_cmd == "setpio") {
        if (main_cmd == "getpio") {
            gaiaSendCommand(CMD_GET_PIO_STATE);
        } else {
            if (args.size() < 3) return;
            uint32_t mask = strtoul(args[1].c_str(), NULL, 0);
            uint32_t state = strtoul(args[2].c_str(), NULL, 0);

            uint8_t payload[8] = {
                static_cast<uint8_t>((mask >> 24) & 0xFF),
                static_cast<uint8_t>((mask >> 16) & 0xFF),
                static_cast<uint8_t>((mask >> 8) & 0xFF),
                static_cast<uint8_t>(mask & 0xFF),
                static_cast<uint8_t>((state >> 24) & 0xFF),
                static_cast<uint8_t>((state >> 16) & 0xFF),
                static_cast<uint8_t>((state >> 8) & 0xFF),
                static_cast<uint8_t>(state & 0xFF)
            };
            gaiaSendCommand(CMD_SET_PIO_CONTROL, payload, 8);
        }
    }
    else if (main_cmd == "getaudiosrc" || main_cmd == "setaudiosrc") {
        if (main_cmd == "getaudiosrc") {
            gaiaSendCommand(CMD_GET_AUDIO_SOURCE);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_SET_AUDIO_SOURCE, payload, 1);
        }
    }
    else if (main_cmd == "enterpairing") {
        gaiaSendCommand(CMD_ENTER_BLUETOOTH_PAIRING_MODE);
    }
    else if (main_cmd == "getpromptlang" || main_cmd == "setpromptlang") {
        if (main_cmd == "getpromptlang") {
            gaiaSendCommand(CMD_GET_AUDIO_PROMPT_LANGUAGE);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val) };
            gaiaSendCommand(CMD_CHANGE_AUDIO_PROMPT_LANGUAGE, payload, 1);
        }
    }
    else if (main_cmd == "getspeech" || main_cmd == "setspeech") {
        if (main_cmd == "getspeech") {
            gaiaSendCommand(CMD_GET_SPEECH_RECOGNITION_CONTROL);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val ? 1 : 0) };
            gaiaSendCommand(CMD_SET_SPEECH_RECOGNITION_CONTROL, payload, 1);
        }
    }
    else if (main_cmd == "getdataendpoint" || main_cmd == "setdataendpoint") {
        if (main_cmd == "getdataendpoint") {
            gaiaSendCommand(CMD_GET_DATA_ENDPOINT_MODE);
        } else {
            if (args.size() < 2) return;
            int val = std::stoi(args[1]);
            uint8_t payload[1] = { static_cast<uint8_t>(val ? 1 : 0) };
            gaiaSendCommand(CMD_SET_DATA_ENDPOINT_MODE, payload, 1);
        }
    }
}
