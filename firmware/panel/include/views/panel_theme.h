#ifndef PANEL_THEME_H
#define PANEL_THEME_H

#include <lvgl.h>

// Android / Material Dark Color Tokens
#define UI_COLOR_BG            lv_color_hex(0x0e1017) // Deep Android dark background
#define UI_COLOR_SURFACE       lv_color_hex(0x181b26) // Card surface background
#define UI_COLOR_SURFACE_LIGHT lv_color_hex(0x222636) // Elevated card/header
#define UI_COLOR_PRIMARY       lv_color_hex(0x00e5ff) // Android Cyan Accent
#define UI_COLOR_SECONDARY     lv_color_hex(0x00e676) // Emerald Green
#define UI_COLOR_ACCENT_PURPLE lv_color_hex(0xab47bc) // Purple
#define UI_COLOR_TEXT_MAIN     lv_color_hex(0xf0f4f8) // White-gray primary text
#define UI_COLOR_TEXT_MUTED    lv_color_hex(0x828ca0) // Secondary muted text
#define UI_COLOR_BORDER        lv_color_hex(0x282c3f) // Subtle border
#define UI_COLOR_DANGER        lv_color_hex(0xff1744) // Red alert

static inline void ui_apply_card_style(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_border_color(obj, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 10, 0); // Modern rounded corners
    lv_obj_set_style_pad_all(obj, 6, 0);
}

static inline void ui_apply_btn_style(lv_obj_t* btn, lv_color_t bg_color) {
    lv_obj_set_style_bg_color(btn, bg_color, 0);
    lv_obj_set_style_radius(btn, 14, 0); // Pill / Android style rounded button
    lv_obj_set_style_shadow_width(btn, 4, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
}

#endif // PANEL_THEME_H
