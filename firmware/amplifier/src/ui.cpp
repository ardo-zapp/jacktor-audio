#include "ui.h"
#include "config.h"
#include "state.h"
#include "power.h"
#include "sensors.h"

#include <U8g2lib.h>
#include <cstring>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

enum class UiScene : uint8_t {
  SPLASH,
  BOOTLOG,
  STANDBY,
  RUN,
  ERROR,
  WARN
};

static UiScene scene = UiScene::SPLASH;
static char clockStr[9] = "00:00:00";
static char dateStr[11] = "1970-01-01";
static bool btMode = false;
static bool spkBig = SPK_DEFAULT_BIG;
static uint8_t bootRows = 0;
static const uint8_t MAX_BOOT_ROWS = 6;
static uint32_t lastDrawMs = 0;

static inline void drawHeader(const char* title) {
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, title);
  u8g2.drawHLine(0, 12, 128);
}

static void drawStandbyScreen() {
  u8g2.clearBuffer();
  drawHeader("STANDBY");

  // Draw 12V voltage top-right with 2 decimals for accuracy
  float v12 = getVoltage12V();
  char v12buf[12];
  snprintf(v12buf, sizeof(v12buf), "%.2fV", v12);
  u8g2.setFont(u8g2_font_6x12_tf);
  int v12W = u8g2.getStrWidth(v12buf);
  u8g2.drawStr(128 - v12W, 10, v12buf);

  // Draw clock center
  u8g2.setFont(u8g2_font_logisoso22_tf);
  u8g2.drawStr(6, 45, clockStr);

  // Draw date bottom
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 62, dateStr);

  u8g2.sendBuffer();
}

static void drawRunScreen() {
  u8g2.clearBuffer();
  drawHeader("AMPLIFIER");

  u8g2.setFont(u8g2_font_6x12_tf);
  int clockW = u8g2.getStrWidth(clockStr);
  u8g2.drawStr(128 - clockW, 10, clockStr);

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 24, btMode ? "IN: BT" : "IN: AUX");
  u8g2.drawStr(64, 24, spkBig ? "SPK: BIG" : "SPK: SMALL");

  char vbuf[16], tbuf[16];
  float v = getVoltageInstant();
  float t = getHeatsinkC();
  snprintf(vbuf, sizeof(vbuf), "V: %.1f", v);
  if (isnan(t)) snprintf(tbuf, sizeof(tbuf), "T: --.-C");
  else snprintf(tbuf, sizeof(tbuf), "T: %.1fC", t);

  u8g2.drawStr(0, 38, vbuf);
  u8g2.drawStr(64, 38, tbuf);

  uint8_t vu = 0;
  analyzerGetVu(vu);
  int vuW = map(vu, 0, 255, 0, 120);
  int vuX = 4, vuY = 60, vuH = 12;
  u8g2.drawFrame(vuX, vuY - vuH, 120, vuH);
  if (vuW > 0) u8g2.drawBox(vuX+1, vuY - vuH + 1, vuW, vuH - 2);

  if (powerSpkProtectFault()) {
    u8g2.drawStr(0, 52, "SPK PROTECT FAIL");
  }

  u8g2.sendBuffer();
}

static void drawSplash(const char* title) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, title ? title : "JACKTOR AMP");
  u8g2.drawHLine(0, 12, 128);
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(10, 40, "Booting...");
  u8g2.sendBuffer();
}

static void drawBootLogLine(const char* label, bool ok) {
  if (bootRows >= MAX_BOOT_ROWS) return;
  int y = 24 + 10 * bootRows;
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, y, label ? label : "?");
  u8g2.drawStr(110, y, ok ? "OK" : "FAIL");
  bootRows++;
}

static void drawError(const char* msg) {
  u8g2.clearBuffer();
  drawHeader("ERROR");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 28, msg ? msg : "Unknown error");
  u8g2.sendBuffer();
}

static void drawWarning(const char* msg) {
  u8g2.clearBuffer();
  drawHeader("NOTICE");
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 28, msg ? msg : "Notice");
  u8g2.sendBuffer();
}

void uiInit() {
  u8g2.begin();
  u8g2.setPowerSave(0);
  scene = powerIsOn() ? UiScene::SPLASH : UiScene::STANDBY;
  lastDrawMs = 0;
}

void uiShowBoot(uint32_t holdMs) {
  scene = UiScene::SPLASH;
  drawSplash(FW_NAME);
  if (holdMs > 0) delay(holdMs);
}

void uiShowFactoryReset(const char* subtitle, uint32_t holdMs) {
  scene = UiScene::WARN;
  u8g2.clearBuffer();
  drawHeader("FACTORY RESET");
  u8g2.setFont(u8g2_font_6x12_tf);
  const char *line = (subtitle && subtitle[0]) ? subtitle : "Menghapus NVS...";
  u8g2.drawStr(0, 32, line);
  u8g2.sendBuffer();
  if (holdMs > 0) delay(holdMs);
}

void uiTick(uint32_t now) {
  if (now - lastDrawMs < 33) return;
  lastDrawMs = now;

  switch (scene) {
    case UiScene::SPLASH: break;
    case UiScene::BOOTLOG: u8g2.sendBuffer(); break;
    case UiScene::STANDBY: drawStandbyScreen(); break;
    case UiScene::RUN: drawRunScreen(); break;
    case UiScene::ERROR: break;
    case UiScene::WARN: break;
  }
}

void uiShowSplash(const char* title) {
  scene = UiScene::SPLASH;
  drawSplash(title);
}

void uiBootLogLine(const char* label, bool ok) {
  if (scene != UiScene::BOOTLOG) {
    scene = UiScene::BOOTLOG;
    u8g2.clearBuffer();
    drawHeader("BOOT LOG");
  }
  drawBootLogLine(label, ok);
  u8g2.sendBuffer();
}

void uiShowError(const char* msg) {
  scene = UiScene::ERROR;
  drawError(msg);
}

void uiShowWarning(const char* msg) {
  scene = UiScene::WARN;
  drawWarning(msg);
}

void uiClearErrorToRun() {
  if (scene == UiScene::ERROR || scene == UiScene::WARN) {
    scene = UiScene::RUN;
  }
}

void uiShowStandby() {
  scene = UiScene::STANDBY;
  drawStandbyScreen();
}

void uiForceStandby() {
  if (scene != UiScene::STANDBY) {
    scene = UiScene::STANDBY;
    drawStandbyScreen();
  }
}

void uiTransitionToRun() {
  if (scene == UiScene::SPLASH || scene == UiScene::BOOTLOG) {
    scene = UiScene::RUN;
  }
}

bool uiIsErrorActive() {
  return (scene == UiScene::ERROR || scene == UiScene::WARN);
}

void uiSetClock(const char* hhmmss) {
  if (!hhmmss) return;
  strncpy(clockStr, hhmmss, sizeof(clockStr)-1);
  clockStr[sizeof(clockStr)-1] = '\0';
}

void uiSetDate(const char* yyyymmdd) {
  if (!yyyymmdd) return;
  strncpy(dateStr, yyyymmdd, sizeof(dateStr)-1);
  dateStr[sizeof(dateStr)-1] = '\0';
}

void uiSetInputStatus(bool bt, bool speakerBig) {
  btMode = bt;
  spkBig = speakerBig;
}