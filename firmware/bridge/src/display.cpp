#include "display.h"
#include "config.h"
#include "comms.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

static TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);

// Gunakan SPI terpisah (VSPI) untuk Touchscreen
static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

// Buffer LVGL 9.x (S3 punya banyak RAM, bisa full buffer / half buffer)
#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
static uint8_t draw_buf[DRAW_BUF_SIZE];

static lv_display_t *disp;
static lv_indev_t *indev;

// UI Objects
static lv_obj_t * label_temp;
static lv_obj_t * label_volt;
static lv_obj_t * label_mode;
static lv_obj_t * bar_vu;

static lv_obj_t * btn_power;
static lv_obj_t * label_power;
static lv_obj_t * btn_spk;
static lv_obj_t * label_spk;
static lv_obj_t * btn_bt;
static lv_obj_t * label_bt;
static lv_obj_t * btn_prev;
static lv_obj_t * btn_play;
static lv_obj_t * btn_next;

// State Tracking for toggles
static bool state_power_on = false;
static bool state_spk_big = true;
static bool state_bt_on = true;

static void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)px_map, w * h, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();

    // Kalibrasi touchscreen (tergantung layar, ini nilai kasar default ILI9341+XPT2046)
    // Map koordinat sentuh mentah (sekitar 200..3800) ke 0..320 dan 0..240
    int16_t x = map(p.x, 200, 3800, 0, TFT_HEIGHT); // layar diputar 90 deg (landscape)
    int16_t y = map(p.y, 200, 3800, 0, TFT_WIDTH);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Callbacks Kontrol Amplifier
static void send_cmd(const char* key, bool val) {
  JsonDocument doc;
  doc["type"] = "cmd";
  doc["cmd"][key] = val;
  commsSendAmpCommand(doc);
}

static void send_cmd_str(const char* key, const char* val) {
  JsonDocument doc;
  doc["type"] = "cmd";
  doc["cmd"][key] = val;
  commsSendAmpCommand(doc);
}

static void btn_power_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    send_cmd("power", !state_power_on);
  }
}

static void btn_spk_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    send_cmd_str("spk_sel", state_spk_big ? "small" : "big");
  }
}

static void btn_bt_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    send_cmd("bt", !state_bt_on);
  }
}

static void btn_btctrl_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    if(btn == btn_prev) send_cmd("bt_prev", true);
    else if(btn == btn_play) send_cmd("bt_play", true);
    else if(btn == btn_next) send_cmd("bt_next", true);
  }
}

static void build_ui() {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x080B0E), LV_PART_MAIN);

  // Bagian Header (Status)
  label_temp = lv_label_create(lv_screen_active());
  lv_label_set_text(label_temp, "Temp: -- C");
  lv_obj_set_style_text_color(label_temp, lv_color_hex(0x00CFFF), LV_PART_MAIN);
  lv_obj_align(label_temp, LV_ALIGN_TOP_LEFT, 5, 5);

  label_volt = lv_label_create(lv_screen_active());
  lv_label_set_text(label_volt, "SMPS: --V | 12V: --V");
  lv_obj_set_style_text_color(label_volt, lv_color_hex(0x00E6FF), LV_PART_MAIN);
  lv_obj_align(label_volt, LV_ALIGN_TOP_LEFT, 5, 25);

  label_mode = lv_label_create(lv_screen_active());
  lv_label_set_text(label_mode, "AUX");
  lv_obj_set_style_text_color(label_mode, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_align(label_mode, LV_ALIGN_TOP_RIGHT, -5, 5);

  // Bar VU
  bar_vu = lv_bar_create(lv_screen_active());
  lv_obj_set_size(bar_vu, 300, 15);
  lv_obj_align(bar_vu, LV_ALIGN_TOP_MID, 0, 50);
  lv_bar_set_range(bar_vu, 0, 255);
  lv_bar_set_value(bar_vu, 0, LV_ANIM_OFF);

  // Row Kontrol Atas (Speaker & BT Toggle)
  btn_spk = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_spk, 120, 40);
  lv_obj_align(btn_spk, LV_ALIGN_CENTER, -70, -10);
  lv_obj_add_event_cb(btn_spk, btn_spk_event_cb, LV_EVENT_ALL, NULL);
  label_spk = lv_label_create(btn_spk);
  lv_label_set_text(label_spk, "SPK: BIG");
  lv_obj_center(label_spk);

  btn_bt = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_bt, 120, 40);
  lv_obj_align(btn_bt, LV_ALIGN_CENTER, 70, -10);
  lv_obj_add_event_cb(btn_bt, btn_bt_event_cb, LV_EVENT_ALL, NULL);
  label_bt = lv_label_create(btn_bt);
  lv_label_set_text(label_bt, "BT: ON");
  lv_obj_center(label_bt);

  // Row BT Controls (Prev, Play, Next)
  btn_prev = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_prev, 60, 40);
  lv_obj_align(btn_prev, LV_ALIGN_CENTER, -80, 40);
  lv_obj_add_event_cb(btn_prev, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_t * lbl = lv_label_create(btn_prev);
  lv_label_set_text(lbl, "<<");
  lv_obj_center(lbl);

  btn_play = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_play, 80, 40);
  lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_event_cb(btn_play, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lbl = lv_label_create(btn_play);
  lv_label_set_text(lbl, "PLAY");
  lv_obj_center(lbl);

  btn_next = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_next, 60, 40);
  lv_obj_align(btn_next, LV_ALIGN_CENTER, 80, 40);
  lv_obj_add_event_cb(btn_next, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lbl = lv_label_create(btn_next);
  lv_label_set_text(lbl, ">>");
  lv_obj_center(lbl);

  // Tombol Power Utama (Bawah)
  btn_power = lv_button_create(lv_screen_active());
  lv_obj_set_size(btn_power, 300, 45);
  lv_obj_align(btn_power, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_add_event_cb(btn_power, btn_power_event_cb, LV_EVENT_ALL, NULL);
  label_power = lv_label_create(btn_power);
  lv_label_set_text(label_power, "POWER");
  lv_obj_center(label_power);
}

void displayInit() {
  // Inisiasi TFT_eSPI
  tft.begin();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);

  // Nyalakan Backlight (PWM / HIGH)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Inisiasi Touch SPI bus
  touchSPI.begin(PIN_TOUCH_SCK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1); // Sesuaikan putaran layar LCD (1=landscape ILI9341)

  // Inisiasi LVGL 9.x
  lv_init();

  // Buat Display Driver baru LVGL 9.x
  disp = lv_display_create(TFT_HEIGHT, TFT_WIDTH); // Landscape: w=320, h=240
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Buat Touch Input Driver baru LVGL 9.x
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  // Buat Antarmuka
  build_ui();
}

void displayTick(uint32_t now) {
  lv_timer_handler(); // memproses animasi dan UI event LVGL
}

void displayUpdateTelemetry(const JsonDocument& doc) {
  if (doc["rt"].is<JsonObject>()) {
    JsonObjectConst rt = doc["rt"];
    int vu = rt["vu"] | 0;
    lv_bar_set_value(bar_vu, vu, LV_ANIM_OFF); // Realtime ~30Hz, matikan animasi bawaan LVGL agar responsif

    const char * mode = rt["input"] | "aux";
    String mStr = String("Mode: ") + mode;
    mStr.toUpperCase();
    lv_label_set_text(label_mode, mStr.c_str());
  }

  if (doc["hz1"].is<JsonObject>()) {
    JsonObjectConst hz1 = doc["hz1"];

    // Suhu
    float tempC = hz1["heat_c"] | 0.0f;
    char tempBuf[32];
    snprintf(tempBuf, sizeof(tempBuf), "Temp: %.1f C", tempC);
    lv_label_set_text(label_temp, tempBuf);

    // Tegangan
    float vSmps = hz1["smps"]["v"] | 0.0f;
    float v12 = hz1["v12"] | 0.0f;
    char voltBuf[64];
    snprintf(voltBuf, sizeof(voltBuf), "SMPS: %.1f V | 12V: %.2f V", vSmps, v12);
    lv_label_set_text(label_volt, voltBuf);

    // Status Power Tracker
    state_power_on = hz1["states"]["on"] | false;
    lv_label_set_text(label_power, state_power_on ? "TURN OFF" : "TURN ON");
    if(state_power_on) {
        lv_obj_set_style_bg_color(btn_power, lv_color_hex(0xAA0000), LV_PART_MAIN); // Merah saat ON
    } else {
        lv_obj_set_style_bg_color(btn_power, lv_color_hex(0x00AA00), LV_PART_MAIN); // Hijau saat OFF
    }

    // Input States (Speaker & BT)
    if (hz1["inputs"].is<JsonObject>()) {
        JsonObjectConst inputs = hz1["inputs"];
        const char* spk_mode = inputs["speaker"] | "small";
        state_spk_big = (strcmp(spk_mode, "big") == 0);
        lv_label_set_text(label_spk, state_spk_big ? "SPK: BIG" : "SPK: SMALL");

        state_bt_on = inputs["bt"] | false;
        lv_label_set_text(label_bt, state_bt_on ? "BT: ON" : "BT: OFF");
    }
  }
}
