#include "panel_main.h"
#include "nes_manager.h"
#include "views/status_bar.h"

bool isUartLinkOk = false;
uint32_t lastTelemetryRxMs = 0;
bool linkBypassed = false;

static char rxBuf[4096];
static size_t rxPos = 0;

static void decodeBase64Bands(const char* input, uint8_t outBands[64], uint8_t &outLen) {
    if (!input) return;
    static const int b64_inv[256] = {
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
        -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
        52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
        -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
        15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
        -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
        41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
    };

    size_t inLen = strlen(input);
    uint32_t val = 0;
    int valb = -8;
    outLen = 0;

    for (size_t i = 0; i < inLen; i++) {
        unsigned char c = (unsigned char)input[i];
        if (b64_inv[c] == -1) continue;
        val = (val << 6) | b64_inv[c];
        valb += 6;
        if (valb >= 0) {
            if (outLen < 64) {
                outBands[outLen++] = (uint8_t)((val >> valb) & 0xFF);
            }
            valb -= 8;
        }
    }
}

void send_uart_command(const char* json_cmd) {
    if (!json_cmd) return;
    size_t len = strlen(json_cmd);
    char* buf = (char*)malloc(len + 2);
    if (buf) {
        memcpy(buf, json_cmd, len);
        buf[len] = '\n';
        buf[len + 1] = '\0';
        uart_write_bytes(UART_NUM_2, buf, len + 1);
        free(buf);
    } else {
        uart_write_bytes(UART_NUM_2, json_cmd, len);
        uart_write_bytes(UART_NUM_2, "\n", 1);
    }
    printf("%s\n", json_cmd);
    fflush(stdout);
    trigger_status_bar_tx_pulse();
}

void panel_send_commands_list() {
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

    static const CommandHelpItem panel_items[] = {
        {"music", "music / player : Launch Music Player & Lyric Visualizer app"},
        {"nes", "nes : Launch Nintendo Entertainment System (NES) emulator"},
        {"msc", "msc : Launch USB Mass Storage mode for SD Card file transfers"},
        {"dac", "dac : Launch 32-bit USB Audio DAC slave mode"},
        {"settings", "settings : Open panel settings (brightness, sleep timer, Wi-Fi)"}
    };
    static const CommandHelpItem pwr_items[] = {
        {"pwr-on", "pwr-on : Power on panel display and system"},
        {"pwr-off", "pwr-off : Power off panel display (Standby mode)"}
    };
    static const CommandHelpItem sys_items[] = {
        {"deviceinfo", "deviceinfo : Display panel hardware and memory statistics"},
        {"rtc-sync", "rtc-sync : Synchronize panel clock with host time"},
        {"reset", "reset : Reboot panel microcontroller"}
    };

    static const CommandCategoryDef panel_categories[] = {
        {"panel", "Front panel apps and display features", panel_items, sizeof(panel_items)/sizeof(panel_items[0])},
        {"power", "Panel power synchronization", pwr_items, sizeof(pwr_items)/sizeof(pwr_items[0])},
        {"system", "Panel diagnostic & system commands", sys_items, sizeof(sys_items)/sizeof(sys_items[0])}
    };

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ver", "1");
    cJSON_AddStringToObject(root, "type", "commands_list");
    cJSON_AddStringToObject(root, "origin", "PANEL");

    cJSON *cats = cJSON_CreateArray();
    cJSON *cmds = cJSON_CreateArray();

    for (size_t c = 0; c < sizeof(panel_categories)/sizeof(panel_categories[0]); c++) {
        const CommandCategoryDef &catDef = panel_categories[c];
        cJSON *catObj = cJSON_CreateObject();
        cJSON_AddStringToObject(catObj, "name", catDef.name);
        cJSON_AddStringToObject(catObj, "desc", catDef.desc);
        cJSON *itemsArr = cJSON_CreateArray();
        for (size_t i = 0; i < catDef.count; i++) {
            cJSON *itemObj = cJSON_CreateObject();
            cJSON_AddStringToObject(itemObj, "cmd", catDef.items[i].cmd);
            cJSON_AddStringToObject(itemObj, "desc", catDef.items[i].desc);
            cJSON_AddItemToArray(itemsArr, itemObj);
            cJSON_AddItemToArray(cmds, cJSON_CreateString(catDef.items[i].cmd));
        }
        cJSON_AddItemToObject(catObj, "items", itemsArr);
        cJSON_AddItemToArray(cats, catObj);
    }

    cJSON_AddItemToObject(root, "categories", cats);
    cJSON_AddItemToObject(root, "commands", cmds);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        send_uart_command(out);
        free(out);
    }
    cJSON_Delete(root);
}

void panel_send_log(const char* level, const char* msg) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ver", "1");
    cJSON_AddStringToObject(root, "type", "log");
    cJSON_AddStringToObject(root, "lvl", level ? level : "info");
    cJSON_AddStringToObject(root, "msg", msg ? msg : "");
    cJSON_AddStringToObject(root, "origin", "PANEL");
    char* out = cJSON_PrintUnformatted(root);
    if (out) {
        send_uart_command(out);
        free(out);
    }
    cJSON_Delete(root);
}

void panel_comms_init() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = AMP_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    uart_driver_install(UART_NUM_2, 8192, 8192, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, AMP_TX_PIN, AMP_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void parse_json_message(const char* json_str) {
    cJSON* root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON* typeItem = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(typeItem) && typeItem->valuestring) {
        std::string msgType = typeItem->valuestring;

        if (msgType == "gamepad") {
            cJSON* pNum = cJSON_GetObjectItem(root, "player");
            int p = (pNum && cJSON_IsNumber(pNum)) ? (int)pNum->valuedouble : 1;

            cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsNumber(state)) {
                nes_manager_update_player_input(p, (uint32_t)state->valuedouble);
            }

            cJSON* p1State = cJSON_GetObjectItem(root, "p1");
            if (cJSON_IsNumber(p1State)) {
                nes_manager_update_player_input(1, (uint32_t)p1State->valuedouble);
            }

            cJSON* p2State = cJSON_GetObjectItem(root, "p2");
            if (cJSON_IsNumber(p2State)) {
                nes_manager_update_player_input(2, (uint32_t)p2State->valuedouble);
            }
        }
        else if (msgType == "get_commands" || msgType == "get_command_list") {
            panel_send_commands_list();
        }
        else if (msgType == "selftest_progress" || msgType == "setup_progress") {
            if (currentScreenState == ScreenState::STANDBY) {
                panel_ui_switch_screen(ScreenState::SELF_TEST);
            }
            cJSON* msg = cJSON_GetObjectItem(root, "msg");
            if (cJSON_IsString(msg) && msg->valuestring) {
                append_boot_log(msg->valuestring);
            }
        }
        else if (msgType == "selftest_report" || msgType == "setup_report") {
            selftestReportReceived = true;
            cJSON* status = cJSON_GetObjectItem(root, "status");
            if (cJSON_IsString(status) && status->valuestring) {
                selftestStepOk = (strcmp(status->valuestring, "PASS") == 0);
            }
            cJSON* error = cJSON_GetObjectItem(root, "error");
            if (cJSON_IsString(error) && error->valuestring) {
                selftestErrorDetail = error->valuestring;
            }
            if (selftestStepOk && currentScreenState == ScreenState::SELF_TEST) {
                panel_ui_switch_screen(ScreenState::OPERATIONAL);
            }
        }
        else if (msgType == "cmd") {
            cJSON* cmdObj = cJSON_GetObjectItem(root, "cmd");
            if (cmdObj && cJSON_IsObject(cmdObj)) {
                cJSON* pwr = cJSON_GetObjectItem(cmdObj, "power");
                if (cJSON_IsBool(pwr)) {
                    if (cJSON_IsTrue(pwr)) {
                        if (currentScreenState == ScreenState::STANDBY) {
                            panel_ui_switch_screen(ScreenState::SELF_TEST);
                        }
                    } else {
                        panel_ui_switch_screen(ScreenState::STANDBY);
                    }
                }
            } else if (cJSON_IsString(cmdObj) && cmdObj->valuestring) {
                if (strcmp(cmdObj->valuestring, "power_on") == 0) {
                    if (currentScreenState == ScreenState::STANDBY) {
                        panel_ui_switch_screen(ScreenState::SELF_TEST);
                    }
                } else if (strcmp(cmdObj->valuestring, "power_off") == 0) {
                    panel_ui_switch_screen(ScreenState::STANDBY);
                }
            }
        }
        else if (msgType == "telemetry") {
            lastTelemetryRxMs = millis();
            isUartLinkOk = true;

            cJSON* stbyV = cJSON_GetObjectItem(root, "stby_voltage");
            if (cJSON_IsNumber(stbyV)) stbyVoltage = (float)stbyV->valuedouble;

            cJSON* vddV = cJSON_GetObjectItem(root, "v_vdd");
            if (!vddV) vddV = cJSON_GetObjectItem(root, "vdd_voltage");
            if (!vddV) vddV = cJSON_GetObjectItem(root, "trafo_voltage");
            if (cJSON_IsNumber(vddV)) vddVoltage = (float)vddV->valuedouble;

            cJSON* espV = cJSON_GetObjectItem(root, "esp_voltage");
            if (cJSON_IsNumber(espV)) espVoltage = (float)espV->valuedouble;

            cJSON* hsTemp = cJSON_GetObjectItem(root, "heatsink_temp");
            if (cJSON_IsNumber(hsTemp)) heatsinkTemp = (float)hsTemp->valuedouble;

            cJSON* rTemp = cJSON_GetObjectItem(root, "rtc_temp");
            if (cJSON_IsNumber(rTemp)) rtcTemp = (float)rTemp->valuedouble;

            cJSON* pOn = cJSON_GetObjectItem(root, "power_on");
            if (cJSON_IsBool(pOn)) amplifierPowerOn = cJSON_IsTrue(pOn);

            cJSON* initSt = cJSON_GetObjectItem(root, "initializing");
            if (cJSON_IsBool(initSt)) isInitializingState = cJSON_IsTrue(initSt);

            cJSON* btFail = cJSON_GetObjectItem(root, "bt_failed_perm");
            if (cJSON_IsBool(btFail)) isBtFailedPerm = cJSON_IsTrue(btFail);

            cJSON* timeStr = cJSON_GetObjectItem(root, "current_time");
            if (cJSON_IsString(timeStr) && timeStr->valuestring) {
                strncpy(currentTimeStr, timeStr->valuestring, sizeof(currentTimeStr) - 1);
            }

            cJSON* rt = cJSON_GetObjectItem(root, "rt");
            if (rt && cJSON_IsObject(rt)) {
                cJSON* vuL = cJSON_GetObjectItem(rt, "vu_l");
                if (cJSON_IsNumber(vuL)) telemetryVuL = (uint8_t)vuL->valuedouble;

                cJSON* vuR = cJSON_GetObjectItem(rt, "vu_r");
                if (cJSON_IsNumber(vuR)) telemetryVuR = (uint8_t)vuR->valuedouble;

                cJSON* btSt = cJSON_GetObjectItem(rt, "bt_state");
                if (cJSON_IsString(btSt) && btSt->valuestring) {
                    isBtInputActive = (strcmp(btSt->valuestring, "bt") == 0);
                }

                cJSON* b64 = cJSON_GetObjectItem(rt, "bands_b64");
                if (cJSON_IsString(b64) && b64->valuestring) {
                    decodeBase64Bands(b64->valuestring, telemetryBands, telemetryBandsLen);
                }
            }

            cJSON* hz1 = cJSON_GetObjectItem(root, "hz1");
            if (hz1 && cJSON_IsObject(hz1)) {
                cJSON* nvs = cJSON_GetObjectItem(hz1, "nvs");
                if (nvs && cJSON_IsObject(nvs)) {
                    cJSON* btEn = cJSON_GetObjectItem(nvs, "bt_en");
                    if (cJSON_IsBool(btEn)) isBtEnabledState = cJSON_IsTrue(btEn);
                }
            }

            cJSON* gPad = cJSON_GetObjectItem(root, "gamepad_state");
            if (cJSON_IsNumber(gPad)) {
                nes_manager_update_input((uint32_t)gPad->valuedouble);
            }

            panel_ui_update_telemetry();
        }
    }

    cJSON_Delete(root);
}

void panel_comms_tick() {
    uint8_t inChunk[256];
    int chunkLen = 0;
    while ((chunkLen = uart_read_bytes(UART_NUM_2, inChunk, sizeof(inChunk), 0)) > 0) {
        trigger_status_bar_rx_pulse();
        for (int i = 0; i < chunkLen; i++) {
            char byteIn = (char)inChunk[i];
            if (byteIn == '\n' || byteIn == '\r') {
                if (rxPos > 0) {
                    rxBuf[rxPos] = '\0';
                    parse_json_message(rxBuf);
                    rxPos = 0;
                }
            } else {
                if (rxPos < sizeof(rxBuf) - 1) {
                    rxBuf[rxPos++] = byteIn;
                } else {
                    rxPos = 0; // Overflow reset
                }
            }
        }
    }

    if (!linkBypassed && (millis() - lastTelemetryRxMs > 3000)) {
        isUartLinkOk = false;
        telemetryVuL = 0;
        telemetryVuR = 0;
        panel_ui_update_telemetry();
    }
}
