#include "panel_main.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <dirent.h>

esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;
esp_lcd_touch_handle_t touch_handle = NULL;
esp_lcd_panel_io_handle_t touch_io_handle = NULL;
static sdmmc_card_t *sd_card = NULL;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    esp_lcd_panel_handle_t lcd_panel = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    esp_lcd_panel_draw_bitmap(lcd_panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

void ili9341_draw_direct_frame(const uint16_t* pixels, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!pixels || !panel_handle) return;
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + w, y + h, pixels);
}

static int16_t last_touch_x = -1;
static int16_t last_touch_y = -1;
static bool is_touch_pressed = false;

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t) drv->user_data;
    esp_lcd_touch_point_data_t pt[1];
    uint8_t count = 0;

    if (tp) {
        esp_lcd_touch_read_data(tp);
        esp_err_t err = esp_lcd_touch_get_data(tp, pt, &count, 1);
        if (err == ESP_OK && count > 0) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = pt[0].x;
            data->point.y = pt[0].y;
            last_touch_x = pt[0].x;
            last_touch_y = pt[0].y;
            is_touch_pressed = true;

            lastTouchActivityMs = millis();
            if (isLcdSleeping) {
                isLcdSleeping = false;
                backlight_set(255);
            }
            return;
        }
    }
    is_touch_pressed = false;
    data->state = LV_INDEV_STATE_REL;
}

bool panel_hardware_get_sd_info(uint64_t* total_bytes, uint64_t* free_bytes) {
    struct statvfs st;
    if (statvfs("/sdcard", &st) == 0) {
        uint64_t total = (uint64_t)st.f_blocks * st.f_frsize;
        uint64_t free_b = (uint64_t)st.f_bfree * st.f_frsize;
        if (total_bytes) *total_bytes = total;
        if (free_bytes) *free_bytes = free_b;
        return true;
    }
    return false;
}

bool panel_hardware_format_sd(void) {
    if (!sd_card) return false;
    esp_err_t ret = esp_vfs_fat_sdcard_format("/sdcard", sd_card);
    if (ret == ESP_OK) {
        struct stat st;
        if (stat("/sdcard/NES", &st) != 0) {
            mkdir("/sdcard/NES", 0775);
        }
        return true;
    }
    return false;
}

uint32_t panel_hardware_get_last_touch_ms(void) {
    return lastTouchActivityMs;
}

bool panel_hardware_get_touch_pos(int16_t* x, int16_t* y) {
    if (touch_handle) {
        esp_lcd_touch_point_data_t pt[1];
        uint8_t count = 0;
        esp_lcd_touch_read_data(touch_handle);
        esp_err_t err = esp_lcd_touch_get_data(touch_handle, pt, &count, 1);
        if (err == ESP_OK && count > 0) {
            last_touch_x = pt[0].x;
            last_touch_y = pt[0].y;
            is_touch_pressed = true;
            lastTouchActivityMs = millis();
            if (x) *x = pt[0].x;
            if (y) *y = pt[0].y;
            return true;
        }
    }
    is_touch_pressed = false;
    if (x) *x = last_touch_x;
    if (y) *y = last_touch_y;
    return false;
}

void backlight_set(uint8_t duty_255) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void init_sd_card_and_nes_dir() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)SD_CS_PIN;
    slot_config.host_id = SPI2_HOST;

    esp_err_t ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);
    if (ret == ESP_OK && sd_card) {
        struct stat st;
        if (stat("/sdcard/NES", &st) != 0) {
            mkdir("/sdcard/NES", 0775);
        }
    } else {
        gpio_set_level((gpio_num_t)SD_CS_PIN, 1);
    }
}

void panel_hardware_init() {
    ESP_LOGI("HARDWARE", "Initializing GPIO outputs and pull-ups...");
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = (1ULL << TOUCH_CS_PIN) | (1ULL << SD_CS_PIN) | (1ULL << TFT_BL_PIN);
    gpio_config(&io_conf);

    gpio_set_level((gpio_num_t)TOUCH_CS_PIN, 1);
    gpio_set_level((gpio_num_t)SD_CS_PIN, 1);
    gpio_set_level((gpio_num_t)TFT_BL_PIN, 1);

    gpio_config_t irq_conf = {};
    irq_conf.intr_type = GPIO_INTR_DISABLE;
    irq_conf.mode = GPIO_MODE_INPUT;
    irq_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    irq_conf.pin_bit_mask = (1ULL << TOUCH_IRQ_PIN);
    gpio_config(&irq_conf);

    gpio_config_t boot_conf = {};
    boot_conf.intr_type = GPIO_INTR_DISABLE;
    boot_conf.mode = GPIO_MODE_INPUT;
    boot_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    boot_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    boot_conf.pin_bit_mask = (1ULL << GPIO_NUM_0);
    gpio_config(&boot_conf);

    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.duty_resolution = LEDC_TIMER_8_BIT;
    ledc_timer.freq_hz = 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {};
    ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel.gpio_num = (gpio_num_t)TFT_BL_PIN;
    ledc_channel.duty = 255;
    ledc_channel.hpoint = 0;
    ledc_channel_config(&ledc_channel);

    ESP_LOGI("HARDWARE", "Initializing Shared SPI Bus (SCK:%d, MOSI:%d, MISO:%d)...", S_SCLK, S_MOSI, S_MISO);
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = S_SCLK;
    buscfg.mosi_io_num = S_MOSI;
    buscfg.miso_io_num = S_MISO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 320 * 10 * sizeof(lv_color_t);
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    // 1. Initialize LCD Display Panel
    ESP_LOGI("HARDWARE", "Initializing ILI9341 LCD via esp_lcd (CS:%d, DC:%d, RST:%d)...", TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = (gpio_num_t)TFT_DC_PIN;
    io_config.cs_gpio_num = (gpio_num_t)TFT_CS_PIN;
    io_config.pclk_hz = 40 * 1000 * 1000; // 40 MHz SPI clock for ILI9341 for stable signals without blur
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = (gpio_num_t)TFT_RST_PIN;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle);

    esp_lcd_panel_reset(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_lcd_panel_init(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(50));
    // Frame Rate Control 0xB1 command parameter 0x00, 0x1B (~60Hz - 70Hz internal panel refresh)
    esp_lcd_panel_io_tx_param(io_handle, 0xB1, (uint8_t[]){0x00, 0x1B}, 2);
    esp_lcd_panel_mirror(panel_handle, false, false);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_disp_on_off(panel_handle, true);
    ESP_LOGI("HARDWARE", "ILI9341 LCD initialized successfully!");

    // 2. Safely initialize XPT2046 Touch Controller without blocking or crashing if unattached
    ESP_LOGI("HARDWARE", "Attempting XPT2046 Touch Controller initialization (CS:%d, IRQ:%d)...", TOUCH_CS_PIN, TOUCH_IRQ_PIN);
    esp_lcd_panel_io_spi_config_t touch_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG((gpio_num_t)TOUCH_CS_PIN);
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &touch_io_config, &touch_io_handle) == ESP_OK) {
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = 320;
        tp_cfg.y_max = 240;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = (gpio_num_t)TOUCH_IRQ_PIN;
        tp_cfg.levels.reset = 0;
        tp_cfg.levels.interrupt = 0;
        tp_cfg.flags.swap_xy = 0;
        tp_cfg.flags.mirror_x = 0;
        tp_cfg.flags.mirror_y = 0;

        esp_err_t err = esp_lcd_touch_new_spi_xpt2046(touch_io_handle, &tp_cfg, &touch_handle);
        if (err != ESP_OK) {
            ESP_LOGW("HARDWARE", "Touchscreen not connected or XPT2046 init skipped (err:%d)", err);
            touch_handle = NULL;
        } else {
            ESP_LOGI("HARDWARE", "XPT2046 Touch Controller initialized successfully!");
        }
    }

    lv_init();
    buf1 = (lv_color_t *)heap_caps_malloc(320 * 10 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_malloc(320 * 10 * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1) {
        buf1 = (lv_color_t *)malloc(320 * 10 * sizeof(lv_color_t));
    }
    if (!buf2) {
        buf2 = (lv_color_t *)malloc(320 * 10 * sizeof(lv_color_t));
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 320 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    indev_drv.user_data = touch_handle;
    lv_indev_drv_register(&indev_drv);

    init_sd_card_and_nes_dir();

    backlight_set(255);

    lastTouchActivityMs = millis();
}

static bool currentUsbIsHostMode = true;

void panel_hardware_set_usb_host_mode(bool is_host_mode) {
    currentUsbIsHostMode = is_host_mode;
}

bool panel_hardware_is_usb_host_mode(void) {
    return currentUsbIsHostMode;
}
