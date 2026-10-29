#include "comms_handlers.h"
#include "comms_internal.h"
#include "comms_telemetry.h"
#include "config.h"
#include "analyzer.h"
#include "buzzer.h"
#include "gaia.h"

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

void handleAnalyzerJson(cJSON *obj) {
  cJSON *cmdItem = cJSON_GetObjectItem(obj, "cmd");
  const char *cmd = (cmdItem && cJSON_IsString(cmdItem)) ? cmdItem->valuestring : "get";

  if (strcmp(cmd, "set") == 0) {
    cJSON *modeItem = cJSON_GetObjectItem(obj, "mode");
    if (modeItem && cJSON_IsString(modeItem)) analyzerSetMode(modeItem->valuestring);

    cJSON *bandsItem = cJSON_GetObjectItem(obj, "bands");
    if (bandsItem && cJSON_IsNumber(bandsItem)) analyzerSetBands(static_cast<uint8_t>(bandsItem->valuedouble));

    cJSON *updateItem = cJSON_GetObjectItem(obj, "update_ms");
    if (updateItem && cJSON_IsNumber(updateItem)) analyzerSetUpdateMs(static_cast<uint16_t>(updateItem->valuedouble));

    analyzerSaveToNvs();
    sendAckOkStr("analyzer", "set", true);
    sendAnalyzerSnapshot("set");
    forceTel = true;
  } else if (strcmp(cmd, "set_params") == 0) {
    cJSON *gl = cJSON_GetObjectItem(obj, "gain_l");
    if (gl && cJSON_IsNumber(gl)) analyzerSetGainL(static_cast<float>(gl->valuedouble));

    cJSON *gr = cJSON_GetObjectItem(obj, "gain_r");
    if (gr && cJSON_IsNumber(gr)) analyzerSetGainR(static_cast<float>(gr->valuedouble));

    cJSON *nl = cJSON_GetObjectItem(obj, "noise_l");
    if (nl && cJSON_IsNumber(nl)) analyzerSetNoiseL(static_cast<float>(nl->valuedouble));

    cJSON *nr = cJSON_GetObjectItem(obj, "noise_r");
    if (nr && cJSON_IsNumber(nr)) analyzerSetNoiseR(static_cast<float>(nr->valuedouble));

    cJSON *rst = cJSON_GetObjectItem(obj, "reset");
    if (rst && cJSON_IsTrue(rst)) analyzerResetGainNoise();

    cJSON *chItem = cJSON_GetObjectItem(obj, "ch");
    if (chItem && cJSON_IsString(chItem) && chItem->valuestring) {
      if (strcmp(chItem->valuestring, "left") == 0) analyzerSetTestChannel(1);
      else if (strcmp(chItem->valuestring, "right") == 0) analyzerSetTestChannel(2);
      else analyzerSetTestChannel(0);
    } else if (chItem && cJSON_IsNumber(chItem)) {
      analyzerSetTestChannel(static_cast<uint8_t>(chItem->valuedouble));
    }

    sendAckOkStr("analyzer", "set_params", true);
    sendAnalyzerSnapshot("set_params");
    forceTel = true;
  } else if (strcmp(cmd, "get") == 0) {
    sendAnalyzerSnapshot("get");
  } else if (strcmp(cmd, "calibrate") == 0) {
    analyzerCalibrateAdc();
    sendAckOkBool("analyzer_calibrate", true, true);
  } else if (strcmp(cmd, "reset_clip") == 0) {
    analyzerResetClippingCount();
    sendAckOkBool("analyzer_reset_clip", true, false);
  } else {
    sendAckErr("analyzer", "invalid_cmd");
  }
}

void handleCmdTone(cJSON *item) {
  if (cJSON_IsObject(item)) {
    cJSON *fItem = cJSON_GetObjectItem(item, "f");
    uint32_t f = (fItem && cJSON_IsNumber(fItem)) ? (uint32_t)fItem->valuedouble : BUZZER_PWM_BASE_FREQ;

    cJSON *dItem = cJSON_GetObjectItem(item, "d");
    uint16_t d = (dItem && cJSON_IsNumber(dItem)) ? (uint16_t)dItem->valuedouble : BUZZER_DUTY_DEFAULT;

    cJSON *msItem = cJSON_GetObjectItem(item, "ms");
    uint16_t msDur = (msItem && cJSON_IsNumber(msItem)) ? (uint16_t)msItem->valuedouble : 60;

    cJSON *pItem = cJSON_GetObjectItem(item, "p");
    uint16_t pauseMs = (pItem && cJSON_IsNumber(pItem)) ? (uint16_t)pItem->valuedouble : 0;

    sendAckOkBool("tone", true, false);
    if (pauseMs > 0) {
      BuzzStep seq[2] = {
        {(uint16_t)f, msDur, (uint16_t)d},
        {0, pauseMs, 0}
      };
      buzzerCustomSequence(seq, 2);
    } else {
      BuzzStep seq[1] = {
        {(uint16_t)f, msDur, (uint16_t)d}
      };
      buzzerCustomSequence(seq, 1);
    }
  } else if (cJSON_IsArray(item)) {
    sendAckOkBool("tone", true, false);
    int count = cJSON_GetArraySize(item);
    BuzzStep seq[24];
    size_t seqLen = 0;
    for (int i = 0; i < count && seqLen < 23; ++i) {
      cJSON *elem = cJSON_GetArrayItem(item, i);
      if (cJSON_IsObject(elem)) {
        cJSON *fItem = cJSON_GetObjectItem(elem, "f");
        uint32_t f = (fItem && cJSON_IsNumber(fItem)) ? (uint32_t)fItem->valuedouble : BUZZER_PWM_BASE_FREQ;

        cJSON *dItem = cJSON_GetObjectItem(elem, "d");
        uint16_t d = (dItem && cJSON_IsNumber(dItem)) ? (uint16_t)dItem->valuedouble : BUZZER_DUTY_DEFAULT;

        cJSON *msItem = cJSON_GetObjectItem(elem, "ms");
        uint16_t msDur = (msItem && cJSON_IsNumber(msItem)) ? (uint16_t)msItem->valuedouble : 60;

        cJSON *pItem = cJSON_GetObjectItem(elem, "p");
        uint16_t pauseMs = (pItem && cJSON_IsNumber(pItem)) ? (uint16_t)pItem->valuedouble : 0;

        seq[seqLen++] = {(uint16_t)f, msDur, (uint16_t)d};
        if (pauseMs > 0 && seqLen < 24) {
          seq[seqLen++] = {0, pauseMs, 0};
        }
      }
    }
    if (seqLen > 0) {
      buzzerCustomSequence(seq, seqLen);
    }
  } else {
    sendAckErr("tone", "invalid");
  }
}
