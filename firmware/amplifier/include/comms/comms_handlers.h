#ifndef COMMS_HANDLERS_H
#define COMMS_HANDLERS_H

#include <string>
#include "driver/uart.h"

void handleJsonLine(const std::string &line, uart_port_t srcPort = UART_NUM_0);
void handleShellCommand(const std::string &line);

// Sub-handler prototypes
void handleCmdOtaBegin(cJSON *item);
void handleCmdOtaWrite(cJSON *item);
void handleCmdOtaEnd(cJSON *item);
void handleCmdOtaAbort(cJSON *item);

void handleAnalyzerJson(cJSON *obj);
void handleCmdTone(cJSON *item);

void handleCmdPower(cJSON *item);
void handleCmdSleepTimer(cJSON *item);
void handleCmdBt(cJSON *item);
void handleCmdAudioRelay(cJSON *item);
void handleCmdDisplayText(cJSON *item);
void handleCmdSpkPwr(cJSON *item);
void handleCmdSmpsBypass(cJSON *item);
void handleCmdSmpsCut(cJSON *item);
void handleCmdSmpsRec(cJSON *item);
void handleCmdBtAutoOff(cJSON *item);
void handleCmdAutoSenseEn(cJSON *item);
void handleCmdAutoSenseTimeout(cJSON *item);
void handleCmdAutoSenseThresh(cJSON *item);
void handleCmdTune(cJSON *item);
void handleCmdSelftest(cJSON *item);
void handleCmdMedia(cJSON *item);
void handleCmdFanMode(cJSON *item);
void handleCmdFanDuty(cJSON *item);
void handleCmdRtcSetEpoch(cJSON *item);
void handleCmdNvsReset(cJSON *item);
void handleCmdFactoryReset(cJSON *item);
void handleCmdAlarmStop(cJSON *item);
void handleCmdBtnLock(cJSON *item);
void handleCmdBtContinue(cJSON *item);
void handleCmdGetStats(cJSON *item);
void handleCmdDeviceInfo(cJSON *item);
void handleCmdPing(cJSON *item);

#endif // COMMS_HANDLERS_H
