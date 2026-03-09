#include "display.h"
#include "config.h"
#include "comms.h"
#include "net.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

static TFT_eSPI tft = TFT_eSPI(TFT_WIDTH, TFT_HEIGHT);

// Gunakan SPI terpisah (HSPI / SPI3) untuk Touchscreen pada ESP32-S3
// S3 tidak memiliki VSPI, melainkan FSPI dan HSPI
static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);

// Full-screen Double Buffering using PSRAM
// 320 x 240 pixels * 2 bytes (RGB565 16-bit) = ~153.6 KB per buffer
#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT * 2)
static uint8_t *draw_buf_1;
static uint8_t *draw_buf_2;

static lv_display_t *disp;
static lv_indev_t *indev;

// UI Objects
static lv_obj_t * label_temp;
static lv_obj_t * label_volt;
static lv_obj_t * label_mode;
static lv_obj_t * bar_vu;
static lv_obj_t * chart;
static lv_chart_series_t * ser;

static lv_obj_t * btn_power;
static lv_obj_t * label_power;
static lv_obj_t * btn_spk;
static lv_obj_t * label_spk;
static lv_obj_t * btn_bt;
static lv_obj_t * label_bt;
static lv_obj_t * btn_prev;
static lv_obj_t * btn_play;
static lv_obj_t * btn_next;
static lv_obj_t * btn_sleep;
static lv_obj_t * label_sleep;

static lv_obj_t * ta_ssid;
static lv_obj_t * ta_pass;
static lv_obj_t * kb;
static lv_obj_t * label_ip;
static lv_obj_t * label_error;

// State Tracking for toggles
static bool state_power_on = false;
static bool state_spk_big = true;
static bool state_bt_on = true;

// State Sleep Timer UI
static const uint32_t sleep_cycles[] = {0, 15, 30, 45, 60, 90, 120};
static uint8_t sleep_idx = 0;

// --- State Backlight & Log Boot ---
static uint16_t logCursorY = 0;
static bool ui_initialized = false;
static bool backlight_state = true;
static int current_pwm = 255;
static int target_pwm = 255;
static uint32_t last_fade_ms = 0;

// Forward declaration untuk touch
static void send_cmd(const char* key, bool val);

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
    // Wake up dari standby bila layar ditekan
    if (!backlight_state) {
        displaySetBacklight(true);
        // Kirim command nyalakan power amp
        send_cmd("power", true);
    }

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

static void btn_sleep_event_cb(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        sleep_idx = (sleep_idx + 1) % (sizeof(sleep_cycles) / sizeof(sleep_cycles[0]));
        send_cmd("sleep_timer", sleep_cycles[sleep_idx]);
    }
}

static void btn_sync_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    netSyncRTC();
  }
}

static void dd_fan_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t * dd = (lv_obj_t *)lv_event_get_target(e);
    char buf[32];
    lv_dropdown_get_selected_str(dd, buf, sizeof(buf));
    String mode(buf);
    mode.toLowerCase();
    send_cmd_str("fan_mode", mode.c_str());
  }
}

static void dd_smps_event_cb(lv_event_t * e) {
  if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t * dd = (lv_obj_t *)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    send_cmd("smps_bypass", sel == 1);
  }
}

static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if(code == LV_EVENT_READY) { // Enter dipencet di keyboard
        const char * ssid = lv_textarea_get_text(ta_ssid);
        const char * pass = lv_textarea_get_text(ta_pass);
        if(strlen(ssid) > 0) {
            netConnectToWifi(String(ssid), String(pass));
        }
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    }
}

void displayBootLog(const char* msg) {
  if (ui_initialized) return; // jangan log jika UI sudah berjalan

  if (logCursorY == 0) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1); // Standard font size
  }

  tft.setCursor(0, logCursorY);
  tft.print(msg);
  tft.println();

  logCursorY += 10;

  // Jika log melebihi layar, reset & scroll manual (simple)
  if (logCursorY >= TFT_WIDTH - 20) { // Layar adalah landscape (320px tinggi aktual log = lebar portrait 240 karena diputar)
    logCursorY = 0;
    tft.fillScreen(TFT_BLACK);
  }
}

void displaySetBacklight(bool on) {
  if (backlight_state != on) {
    backlight_state = on;
    target_pwm = on ? 255 : 0;
  }
}

static void handle_backlight_fade() {
  if (current_pwm != target_pwm) {
    uint32_t now = millis();
    if (now - last_fade_ms > 2) { // 2ms per step
      if (current_pwm < target_pwm) current_pwm++;
      else current_pwm--;

      ledcWrite(0, current_pwm); // channel 0
      last_fade_ms = now;
    }
  }
}

static void build_ui() {
  // Global Background with Gradient and glow style
  lv_obj_t * screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x040608), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x0A1018), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN);

  // Create default button style for modern look (glow + rounded + transitions)
  static lv_style_t style_btn;
  lv_style_init(&style_btn);
  lv_style_set_radius(&style_btn, 8);
  lv_style_set_bg_opa(&style_btn, 255);
  lv_style_set_bg_color(&style_btn, lv_color_hex(0x112233));
  lv_style_set_bg_grad_color(&style_btn, lv_color_hex(0x0A1520));
  lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&style_btn, 1);
  lv_style_set_border_color(&style_btn, lv_color_hex(0x00CFFF));
  lv_style_set_border_opa(&style_btn, LV_OPA_50);
  lv_style_set_shadow_width(&style_btn, 10);
  lv_style_set_shadow_color(&style_btn, lv_color_hex(0x00CFFF));
  lv_style_set_shadow_opa(&style_btn, LV_OPA_0); // Glow off initially
  lv_style_set_text_color(&style_btn, lv_color_hex(0xFFFFFF));

  // Transition settings for smooth button pressing
  static const lv_style_prop_t props[] = {LV_STYLE_SHADOW_OPA, LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV};
  static lv_style_transition_dsc_t trans;
  lv_style_transition_dsc_init(&trans, props, lv_anim_path_ease_in_out, 200, 0, NULL);
  lv_style_set_transition(&style_btn, &trans);

  // Pressed style overrides
  static lv_style_t style_btn_pr;
  lv_style_init(&style_btn_pr);
  lv_style_set_shadow_opa(&style_btn_pr, LV_OPA_80); // Glow turns on
  lv_style_set_bg_color(&style_btn_pr, lv_color_hex(0x005577)); // Lighter bg
  lv_style_set_border_color(&style_btn_pr, lv_color_hex(0x00FFFF));

  // Helper macro to apply modern style
  #define APPLY_BTN_STYLE(btn) \
    lv_obj_add_style(btn, &style_btn, LV_PART_MAIN); \
    lv_obj_add_style(btn, &style_btn_pr, LV_PART_MAIN | LV_STATE_PRESSED)

  // Bagian Header (Status)
  label_temp = lv_label_create(screen);
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

  // Label Peringatan Error (Disembunyikan default)
  label_error = lv_label_create(screen);
  lv_label_set_text(label_error, "");
  lv_obj_set_style_text_color(label_error, lv_color_hex(0xFF0000), LV_PART_MAIN); // Merah terang
  lv_obj_align(label_error, LV_ALIGN_TOP_RIGHT, -5, 25);

  // Tabview Header (Bottom or Top) di LVGL 9
  lv_obj_t * tabview = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);

  lv_obj_t * tab_bar = lv_tabview_get_tab_bar(tabview);
  lv_obj_set_height(tab_bar, 40);

  lv_obj_t * tab_home = lv_tabview_add_tab(tabview, LV_SYMBOL_HOME " Home");
  lv_obj_t * tab_analyzer = lv_tabview_add_tab(tabview, LV_SYMBOL_AUDIO " Analyzer");
  lv_obj_t * tab_settings = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS " Settings");

  // === TAB ANALYZER ===
  chart = lv_chart_create(tab_analyzer);
  lv_obj_set_size(chart, 300, 140);
  lv_obj_center(chart);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 255);
  lv_chart_set_point_count(chart, 32);
  ser = lv_chart_add_series(chart, lv_color_hex(0x00CFFF), LV_CHART_AXIS_PRIMARY_Y);

  // Default bar values using direct array mutation (LVGL 9 compatibility)
  int32_t * init_y = lv_chart_get_y_array(chart, ser);
  if (init_y) {
      for(int i=0; i<32; i++) {
          init_y[i] = 0;
      }
      lv_chart_refresh(chart);
  }

  // === TAB SETTINGS ===
  lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);

  label_ip = lv_label_create(tab_settings);
  lv_label_set_text(label_ip, "IP: Disconnected");

  lv_obj_t * row_btn = lv_obj_create(tab_settings);
  lv_obj_set_size(row_btn, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(row_btn, 0, 0);
  lv_obj_set_style_border_width(row_btn, 0, 0);
  lv_obj_set_style_bg_opa(row_btn, 0, 0);

  lv_obj_t * btn_sync = lv_button_create(row_btn);
  lv_obj_add_event_cb(btn_sync, btn_sync_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_t * lbl_sync = lv_label_create(btn_sync);
  lv_label_set_text(lbl_sync, LV_SYMBOL_REFRESH " Sync NTP");

  // Dropdown Fan
  lv_obj_t * dd_fan = lv_dropdown_create(row_btn);
  lv_dropdown_set_options(dd_fan, "Auto\nCustom\nFailsafe");
  lv_obj_add_event_cb(dd_fan, dd_fan_event_cb, LV_EVENT_ALL, NULL);

  // Dropdown SMPS
  lv_obj_t * dd_smps = lv_dropdown_create(row_btn);
  lv_dropdown_set_options(dd_smps, "SMPS ON\nBypass");
  lv_obj_add_event_cb(dd_smps, dd_smps_event_cb, LV_EVENT_ALL, NULL);

  // WiFi Inputs
  ta_ssid = lv_textarea_create(tab_settings);
  lv_textarea_set_one_line(ta_ssid, true);
  lv_textarea_set_placeholder_text(ta_ssid, "SSID");
  lv_obj_set_width(ta_ssid, LV_PCT(100));
  lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

  ta_pass = lv_textarea_create(tab_settings);
  lv_textarea_set_one_line(ta_pass, true);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_textarea_set_placeholder_text(ta_pass, "Password");
  lv_obj_set_width(ta_pass, LV_PCT(100));
  lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

  // Keyboard (Hidden by default, attaches to active text area)
  kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  // === TAB HOME ===
  bar_vu = lv_bar_create(tab_home);
  lv_obj_set_size(bar_vu, 280, 15);
  lv_obj_align(bar_vu, LV_ALIGN_TOP_MID, 0, 30);
  lv_bar_set_range(bar_vu, 0, 255);
  lv_bar_set_value(bar_vu, 0, LV_ANIM_OFF);

  btn_spk = lv_button_create(tab_home);
  lv_obj_set_size(btn_spk, 110, 40);
  lv_obj_align(btn_spk, LV_ALIGN_CENTER, -60, -10);
  APPLY_BTN_STYLE(btn_spk);
  lv_obj_add_event_cb(btn_spk, btn_spk_event_cb, LV_EVENT_ALL, NULL);
  label_spk = lv_label_create(btn_spk);
  lv_label_set_text(label_spk, LV_SYMBOL_VOLUME_MID " SPK:BIG");
  lv_obj_center(label_spk);

  btn_bt = lv_button_create(tab_home);
  lv_obj_set_size(btn_bt, 110, 40);
  lv_obj_align(btn_bt, LV_ALIGN_CENTER, 60, -10);
  APPLY_BTN_STYLE(btn_bt);
  lv_obj_add_event_cb(btn_bt, btn_bt_event_cb, LV_EVENT_ALL, NULL);
  label_bt = lv_label_create(btn_bt);
  lv_label_set_text(label_bt, LV_SYMBOL_BLUETOOTH " BT:ON");
  lv_obj_center(label_bt);

  // Sleep Timer Button
  btn_sleep = lv_button_create(tab_home);
  lv_obj_set_size(btn_sleep, 90, 30);
  lv_obj_align(btn_sleep, LV_ALIGN_TOP_RIGHT, 0, 5);
  APPLY_BTN_STYLE(btn_sleep);
  lv_obj_add_event_cb(btn_sleep, btn_sleep_event_cb, LV_EVENT_ALL, NULL);
  label_sleep = lv_label_create(btn_sleep);
  lv_label_set_text(label_sleep, LV_SYMBOL_BELL " SLP:OFF");
  lv_obj_center(label_sleep);

  // Row BT Controls (Prev, Play, Next)
  btn_prev = lv_button_create(tab_home);
  lv_obj_set_size(btn_prev, 60, 40);
  lv_obj_align(btn_prev, LV_ALIGN_CENTER, -80, 40);
  APPLY_BTN_STYLE(btn_prev);
  lv_obj_add_event_cb(btn_prev, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_t * lbl = lv_label_create(btn_prev);
  lv_label_set_text(lbl, LV_SYMBOL_PREV);
  lv_obj_center(lbl);

  btn_play = lv_button_create(tab_home);
  lv_obj_set_size(btn_play, 80, 40);
  lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 40);
  APPLY_BTN_STYLE(btn_play);
  lv_obj_add_event_cb(btn_play, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lbl = lv_label_create(btn_play);
  lv_label_set_text(lbl, LV_SYMBOL_PLAY);
  lv_obj_center(lbl);

  btn_next = lv_button_create(tab_home);
  lv_obj_set_size(btn_next, 60, 40);
  lv_obj_align(btn_next, LV_ALIGN_CENTER, 80, 40);
  APPLY_BTN_STYLE(btn_next);
  lv_obj_add_event_cb(btn_next, btn_btctrl_event_cb, LV_EVENT_ALL, NULL);
  lbl = lv_label_create(btn_next);
  lv_label_set_text(lbl, LV_SYMBOL_NEXT);
  lv_obj_center(lbl);

  // Tombol Power Utama (Bawah Tab Home)
  btn_power = lv_button_create(tab_home);
  lv_obj_set_size(btn_power, 280, 40);
  lv_obj_align(btn_power, LV_ALIGN_BOTTOM_MID, 0, -5);
  APPLY_BTN_STYLE(btn_power);
  lv_obj_add_event_cb(btn_power, btn_power_event_cb, LV_EVENT_ALL, NULL);
  label_power = lv_label_create(btn_power);
  lv_label_set_text(label_power, LV_SYMBOL_POWER " POWER");
  lv_obj_center(label_power);
}

void displayInit() {
  // Inisiasi TFT_eSPI
  tft.begin();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);

  // Nyalakan Backlight menggunakan LEDC PWM agar bisa animasi meredup (fade out)
  // Walaupun Core 3.0 menggunakan ledcAttach, PlatformIO ESP32 (framework-arduinoespressif32)
  // yang ditarik saat ini masih v2.x. Kita akan fallback menggunakan API v2.x (ledcSetup).
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, 255);
  current_pwm = target_pwm = 255;
  backlight_state = true;
  ui_initialized = false;

  displayBootLog("Starting Jacktor OS (ESP32-S3)...");
  displayBootLog("[ OK ] Display & PWM Initialized.");

  // Inisiasi Touch SPI bus
  touchSPI.begin(PIN_TOUCH_SCK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1); // Sesuaikan putaran layar LCD (1=landscape ILI9341)

  // Inisiasi LVGL 9.x
  lv_init();

  // Set LVGL tick provider to Arduino millis()
  lv_tick_set_cb((lv_tick_get_cb_t)millis);

  // Alokasikan memori frame buffer ke PSRAM
  displayBootLog("[ WAIT ] Allocating Double Buffers in PSRAM...");
  draw_buf_1 = (uint8_t*)heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  draw_buf_2 = (uint8_t*)heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (draw_buf_1 == NULL || draw_buf_2 == NULL) {
      displayBootLog("[ ERR ] PSRAM Allocation Failed! Fallback to internal RAM...");
      // Fallback ke SRAM jika PSRAM gagal diinisialisasi
      draw_buf_1 = (uint8_t*)heap_caps_malloc(DRAW_BUF_SIZE / 10, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      draw_buf_2 = NULL;
      disp = lv_display_create(TFT_HEIGHT, TFT_WIDTH);
      lv_display_set_flush_cb(disp, my_disp_flush);
      lv_display_set_buffers(disp, draw_buf_1, NULL, DRAW_BUF_SIZE / 10, LV_DISPLAY_RENDER_MODE_PARTIAL);
  } else {
      displayBootLog("[ OK ] PSRAM Allocated. Full-screen Direct Mode Enabled.");
      disp = lv_display_create(TFT_HEIGHT, TFT_WIDTH); // Landscape: w=320, h=240
      lv_display_set_flush_cb(disp, my_disp_flush);
      // Menggunakan FULL render mode untuk meminimalisir tearing karena kita pakai double buffer 100% ukuran layar
      lv_display_set_buffers(disp, draw_buf_1, draw_buf_2, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
  }

  // Buat Touch Input Driver baru LVGL 9.x
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  // Buat Antarmuka
  displayBootLog("[ WAIT ] Generating LVGL Interface...");
  build_ui();
}

void displayStartUI() {
  displayBootLog("[ OK ] System Ready.");
  // Berikan jeda sebentar agar log terakhir terlihat sebelum ditutup UI
  delay(800);
  ui_initialized = true;
}

void displayTick(uint32_t now) {
  if (ui_initialized) {
    lv_timer_handler(); // Selalu jalankan agar indev (touch) tetap bisa pooling polling walau layar mati

    // Update UI Dinamis
    if (backlight_state) {
      static uint32_t last_ip_update = 0;
      if (now - last_ip_update > 1000) {
        String ip = netGetIP();
        lv_label_set_text_fmt(label_ip, "IP: %s", ip.c_str());
        last_ip_update = now;
      }
    }
  }
  handle_backlight_fade();
}

void displayUpdateTelemetry(const JsonDocument& doc) {
  if (doc["rt"].is<JsonObject>()) {
    JsonObjectConst rt = doc["rt"];
    int vu = rt["vu"] | 0;
    lv_bar_set_value(bar_vu, vu, LV_ANIM_OFF); // Realtime ~30Hz, matikan animasi bawaan LVGL agar responsif

    const char * mode = rt["input"] | "aux";
    String mStr = String(mode);
    mStr.toUpperCase();
    lv_label_set_text(label_mode, mStr.c_str());

    // Update 32 Band Analyzer
    if (rt["bands"].is<JsonArray>()) {
      JsonArrayConst bands = rt["bands"];
      size_t count = bands.size();
      if (count > 32) count = 32;

      // lv_chart_set_value_by_id dihapus di LVGL 9, cara baru adalah mengakses array-nya langsung
      int32_t * y_array = lv_chart_get_y_array(chart, ser);
      if (y_array) {
          for (size_t i=0; i<count; i++) {
              y_array[i] = bands[i].as<int32_t>();
          }
          lv_chart_refresh(chart);
      }
    }
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

    // Update Sleep Timer Tracker
    uint32_t sleep_remaining = hz1["sleep_timer"] | 0;
    if (sleep_remaining == 0) {
        lv_label_set_text(label_sleep, LV_SYMBOL_BELL " SLP:OFF");
        lv_obj_set_style_text_color(label_sleep, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    } else {
        char slpBuf[32];
        snprintf(slpBuf, sizeof(slpBuf), LV_SYMBOL_BELL " %dm", sleep_remaining);
        lv_label_set_text(label_sleep, slpBuf);
        lv_obj_set_style_text_color(label_sleep, lv_color_hex(0xFF8800), LV_PART_MAIN); // Orange jika aktif
    }

    // Parsing Errors
    if (hz1["errors"].is<JsonArray>()) {
        JsonArrayConst errs = hz1["errors"];
        if (errs.size() > 0) {
            String err_str = LV_SYMBOL_WARNING " ";
            for (size_t i=0; i<errs.size(); i++) {
                err_str += errs[i].as<String>();
                if (i < errs.size() - 1) err_str += ", ";
            }
            lv_label_set_text(label_error, err_str.c_str());
            lv_obj_remove_flag(label_error, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(label_error, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(label_error, LV_OBJ_FLAG_HIDDEN);
    }
  }
}
