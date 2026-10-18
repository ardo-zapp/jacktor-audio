#include "panel_main.h"
#include "nes_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

extern "C" void app_main(void) {
    // Set stdin to non-blocking mode for USB CDC Console reading
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    panel_hardware_init();
    panel_comms_init();
    usb_dac_driver_init();
    panel_ui_init();

    // Send boot log and command list over USB CDC upon boot
    panel_send_log("info", "JACKTOR S3 Panel initialized (USB CDC Console Ready)");
    panel_send_commands_list();

    uint32_t last_tick = millis();
    static char cdc_buf[512];
    static size_t cdc_pos = 0;

    while (1) {
        // Read lines from USB CDC stdin
        char c = 0;
        while (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') {
                if (cdc_pos > 0) {
                    cdc_buf[cdc_pos] = '\0';
                    parse_json_message(cdc_buf);
                    cdc_pos = 0;
                }
            } else {
                if (cdc_pos < sizeof(cdc_buf) - 1) {
                    cdc_buf[cdc_pos++] = c;
                } else {
                    cdc_pos = 0;
                }
            }
        }
        uint32_t now = millis();
        uint32_t diff = now - last_tick;
        if (diff > 0) {
            lv_tick_inc(diff);
            last_tick = now;
        }

        panel_comms_tick();
        usb_dac_driver_tick();

        // Monitor BOOT button (GPIO 0) press to wake up LCD and switch to Operational
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            isLcdSleeping = false;
            gpio_set_level((gpio_num_t)TFT_BL_PIN, 1);
            backlight_set(255);
            isIndependentBypassMode = true;
            if (currentScreenState == ScreenState::STANDBY) {
                panel_ui_switch_screen(ScreenState::OPERATIONAL);
            }
        }

        if (nes_manager_check_pending_exit()) {
            nes_manager_set_pending_exit(false);
            panel_ui_request_exit_game();
        }

        if (!nes_manager_is_running()) {
            lv_timer_handler();
        }

        if (sleepTimeoutMs > 0 && !isLcdSleeping && (now - lastTouchActivityMs > sleepTimeoutMs)) {
            isLcdSleeping = true;
            backlight_set(0);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
