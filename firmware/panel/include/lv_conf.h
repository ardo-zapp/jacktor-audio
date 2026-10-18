#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1

#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (64U * 1024U)
#define LV_MEM_ADR         0

#define LV_TICK_CUSTOM     1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000ULL))
#endif

/* Widget usage */
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ARC          1
#define LV_USE_SLIDER       1
#define LV_USE_CHECKBOX     1
#define LV_USE_MBOX         1
#define LV_USE_TEXTAREA     1
#define LV_USE_CANVAS       1

/* Fonts */
#define LV_FONT_MONO_8     1
#define LV_FONT_MONO_12    1
#define LV_FONT_DEJAVU_16_PERS_SUBSET 0
#define LV_FONT_SUBPX_BGR  0
#define LV_FONT_DEFAULT    &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_USE_FONT_PLACEHOLDER 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_FONT_MONTSERRAT_8     1
#define LV_FONT_MONTSERRAT_10    1
#define LV_FONT_MONTSERRAT_12    1
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_18    1
#define LV_FONT_MONTSERRAT_20    1
#define LV_FONT_MONTSERRAT_22    1
#define LV_FONT_MONTSERRAT_24    1

#endif /*LV_CONF_H*/
