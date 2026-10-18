#include "ui_file_manager.h"
#include "panel_theme.h"
#include "panel_main.h"
#include "driver/saf_manager.h"

lv_obj_t* fileManagerScreen = nullptr;

static lv_obj_t* fmHomeContainer = nullptr;
static lv_obj_t* fmExplorerContainer = nullptr;

static lv_obj_t* fmListContainer = nullptr;
static lv_obj_t* fmPathLabel = nullptr;

static std::string clipboardPath = "";
static std::string currentFmPath = "/sdcard";
static std::string selectedItemPath = "";

static void refresh_file_manager_list(void);
static void refresh_file_manager_drives(void);

static void show_format_drive_dialog(SafStorageSource src) {
    lv_obj_t* mbox = lv_msgbox_create(NULL, "Format Drive", "Select filesystem format type for drive:", NULL, true);
    lv_obj_center(mbox);

    lv_obj_t* btnFat32 = lv_btn_create(mbox);
    lv_obj_set_size(btnFat32, 90, 28);
    lv_obj_align(btnFat32, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    ui_apply_btn_style(btnFat32, UI_COLOR_PRIMARY);
    lv_obj_t* lblFat = lv_label_create(btnFat32);
    lv_label_set_text(lblFat, "FAT32");
    lv_obj_set_style_text_color(lblFat, lv_color_hex(0x000000), 0);
    lv_obj_center(lblFat);

    lv_obj_t* btnExFat = lv_btn_create(mbox);
    lv_obj_set_size(btnExFat, 90, 28);
    lv_obj_align(btnExFat, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    ui_apply_btn_style(btnExFat, UI_COLOR_SECONDARY);
    lv_obj_t* lblEx = lv_label_create(btnExFat);
    lv_label_set_text(lblEx, "exFAT");
    lv_obj_set_style_text_color(lblEx, lv_color_hex(0x000000), 0);
    lv_obj_center(lblEx);

    lv_obj_add_event_cb(btnFat32, [](lv_event_t* e) {
        saf_format_drive(SAF_SOURCE_SD_CARD, SAF_FS_FAT32);
        lv_obj_t* box = lv_obj_get_parent(lv_event_get_target(e));
        if (box) lv_msgbox_close(box);
        refresh_file_manager_drives();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(btnExFat, [](lv_event_t* e) {
        saf_format_drive(SAF_SOURCE_SD_CARD, SAF_FS_EXFAT);
        lv_obj_t* box = lv_obj_get_parent(lv_event_get_target(e));
        if (box) lv_msgbox_close(box);
        refresh_file_manager_drives();
    }, LV_EVENT_CLICKED, NULL);
}

static void refresh_file_manager_drives(void) {
    if (!fmHomeContainer) return;
    lv_obj_clean(fmHomeContainer);

    std::vector<SafDriveInfo> drives = saf_get_available_drives();

    for (size_t i = 0; i < drives.size(); i++) {
        const auto& drive = drives[i];

        lv_obj_t* card = lv_obj_create(fmHomeContainer);
        lv_obj_set_size(card, 290, 52);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, (int)i * 58);
        ui_apply_card_style(card);

        lv_obj_t* icon = lv_label_create(card);
        lv_label_set_text(icon, (drive.source == SAF_SOURCE_SMB) ? LV_SYMBOL_WIFI : LV_SYMBOL_DIRECTORY);
        lv_obj_set_style_text_color(icon, drive.mounted ? UI_COLOR_PRIMARY : UI_COLOR_TEXT_MUTED, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t* titleLbl = lv_label_create(card);
        lv_label_set_text(titleLbl, drive.label.c_str());
        lv_obj_set_style_text_color(titleLbl, UI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 32, 2);

        char capBuf[80];
        if (drive.mounted && drive.total_bytes > 0) {
            float used_gb = (float)(drive.total_bytes - drive.free_bytes) / (1024.0f * 1024.0f * 1024.0f);
            float total_gb = (float)drive.total_bytes / (1024.0f * 1024.0f * 1024.0f);
            snprintf(capBuf, sizeof(capBuf), "Used: %.1fGB / %.1fGB | %s", used_gb, total_gb, drive.fs_type.c_str());
        } else {
            snprintf(capBuf, sizeof(capBuf), "Drive Disconnected / Unmounted");
        }

        lv_obj_t* capLbl = lv_label_create(card);
        lv_label_set_text(capLbl, capBuf);
        lv_obj_set_style_text_color(capLbl, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(capLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(capLbl, LV_ALIGN_BOTTOM_LEFT, 32, -2);

        if (drive.mounted) {
            lv_obj_t* btnOpen = lv_btn_create(card);
            lv_obj_set_size(btnOpen, 54, 26);
            lv_obj_align(btnOpen, LV_ALIGN_RIGHT_MID, -2, 0);
            ui_apply_btn_style(btnOpen, UI_COLOR_PRIMARY);

            lv_obj_t* lblOpen = lv_label_create(btnOpen);
            lv_label_set_text(lblOpen, "OPEN");
            lv_obj_set_style_text_color(lblOpen, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(lblOpen, &lv_font_montserrat_10, 0);
            lv_obj_center(lblOpen);

            SafStorageSource driveSrc = drive.source;
            lv_obj_add_event_cb(btnOpen, [](lv_event_t* e) {
                SafStorageSource src = (SafStorageSource)(uintptr_t)lv_event_get_user_data(e);
                saf_set_active_source(src);

                if (src == SAF_SOURCE_SD_CARD) currentFmPath = "/sdcard";
                else if (src == SAF_SOURCE_USB_MASS_STORAGE) currentFmPath = "/usbstorage";
                else if (src == SAF_SOURCE_SMB) currentFmPath = "/smb";

                if (fmHomeContainer) lv_obj_add_flag(fmHomeContainer, LV_OBJ_FLAG_HIDDEN);
                if (fmExplorerContainer) lv_obj_clear_flag(fmExplorerContainer, LV_OBJ_FLAG_HIDDEN);
                refresh_file_manager_list();
            }, LV_EVENT_CLICKED, (void*)(uintptr_t)driveSrc);

            if (drive.source == SAF_SOURCE_SD_CARD || drive.source == SAF_SOURCE_USB_MASS_STORAGE) {
                lv_obj_t* btnFmt = lv_btn_create(card);
                lv_obj_set_size(btnFmt, 52, 26);
                lv_obj_align(btnFmt, LV_ALIGN_RIGHT_MID, -60, 0);
                ui_apply_btn_style(btnFmt, UI_COLOR_DANGER);

                lv_obj_t* lblFmt = lv_label_create(btnFmt);
                lv_label_set_text(lblFmt, "Format");
                lv_obj_set_style_text_font(lblFmt, &lv_font_montserrat_10, 0);
                lv_obj_center(lblFmt);

                lv_obj_add_event_cb(btnFmt, [](lv_event_t* e) {
                    SafStorageSource src = (SafStorageSource)(uintptr_t)lv_event_get_user_data(e);
                    show_format_drive_dialog(src);
                }, LV_EVENT_CLICKED, (void*)(uintptr_t)driveSrc);
            }
        }
    }
}

static void refresh_file_manager_list(void) {
    if (!fmListContainer) return;
    lv_obj_clean(fmListContainer);

    if (fmPathLabel) {
        lv_label_set_text(fmPathLabel, currentFmPath.c_str());
    }

    SafStorageSource src = saf_get_active_source();
    std::vector<SafFileEntry> entries = saf_list_directory(currentFmPath, src);

    if (entries.empty()) {
        lv_obj_t* emptyLbl = lv_label_create(fmListContainer);
        lv_label_set_text(emptyLbl, "Directory is empty");
        lv_obj_set_style_text_color(emptyLbl, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_center(emptyLbl);
        return;
    }

    for (size_t i = 0; i < entries.size(); i++) {
        const auto& item = entries[i];

        lv_obj_t* rowBtn = lv_btn_create(fmListContainer);
        lv_obj_set_size(rowBtn, 290, 32);
        lv_obj_align(rowBtn, LV_ALIGN_TOP_MID, 0, (int)i * 36);

        if (item.full_path == selectedItemPath) {
            ui_apply_btn_style(rowBtn, lv_color_hex(0x00838f));
        } else {
            ui_apply_btn_style(rowBtn, UI_COLOR_SURFACE);
        }

        lv_obj_t* icon = lv_label_create(rowBtn);
        lv_label_set_text(icon, item.is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(icon, item.is_directory ? UI_COLOR_PRIMARY : UI_COLOR_SECONDARY, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t* nameLbl = lv_label_create(rowBtn);
        lv_label_set_text(nameLbl, item.name.c_str());
        lv_obj_set_style_text_color(nameLbl, UI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 28, 0);

        char* item_path_copy = strdup(item.full_path.c_str());

        lv_obj_add_event_cb(rowBtn, [](lv_event_t* e) {
            char* path = (char*)lv_event_get_user_data(e);
            if (!path) return;

            SafFileEntry fe;
            if (saf_stat_file(path, &fe) && fe.is_directory) {
                currentFmPath = path;
                selectedItemPath = "";
            } else {
                selectedItemPath = path;
            }
            refresh_file_manager_list();
        }, LV_EVENT_CLICKED, item_path_copy);

        lv_obj_add_event_cb(rowBtn, [](lv_event_t* e) {
            char* path = (char*)lv_event_get_user_data(e);
            if (path) free(path);
        }, LV_EVENT_DELETE, item_path_copy);
    }
}

void create_file_manager_screen(void) {
    if (fileManagerScreen) return;

    fileManagerScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(fileManagerScreen, UI_COLOR_BG, 0);

    // Header bar
    lv_obj_t* header = lv_obj_create(fileManagerScreen);
    lv_obj_set_size(header, 320, 36);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(header, 0, 0);

    lv_obj_t* btnBack = lv_btn_create(header);
    lv_obj_set_size(btnBack, 54, 28);
    lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 4, 0);
    ui_apply_btn_style(btnBack, UI_COLOR_SURFACE);
    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, LV_SYMBOL_LEFT " HOME");
    lv_obj_center(lblBack);

    lv_obj_add_event_cb(btnBack, [](lv_event_t* e) {
        if (fmExplorerContainer && !lv_obj_has_flag(fmExplorerContainer, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(fmExplorerContainer, LV_OBJ_FLAG_HIDDEN);
            if (fmHomeContainer) lv_obj_clear_flag(fmHomeContainer, LV_OBJ_FLAG_HIDDEN);
            refresh_file_manager_drives();
        } else {
            panel_ui_switch_screen(ScreenState::OPERATIONAL);
        }
    }, LV_EVENT_CLICKED, NULL);

    fmPathLabel = lv_label_create(header);
    lv_label_set_text(fmPathLabel, "STORAGE DRIVES");
    lv_obj_set_style_text_color(fmPathLabel, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(fmPathLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(fmPathLabel, LV_ALIGN_LEFT_MID, 62, 0);

    // 1. HOME DRIVES CONTAINER
    fmHomeContainer = lv_obj_create(fileManagerScreen);
    lv_obj_set_size(fmHomeContainer, 310, 196);
    lv_obj_align(fmHomeContainer, LV_ALIGN_TOP_LEFT, 5, 40);
    ui_apply_card_style(fmHomeContainer);

    // 2. EXPLORER CONTAINER
    fmExplorerContainer = lv_obj_create(fileManagerScreen);
    lv_obj_set_size(fmExplorerContainer, 320, 204);
    lv_obj_align(fmExplorerContainer, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(fmExplorerContainer, UI_COLOR_BG, 0);
    lv_obj_add_flag(fmExplorerContainer, LV_OBJ_FLAG_HIDDEN);

    fmListContainer = lv_obj_create(fmExplorerContainer);
    lv_obj_set_size(fmListContainer, 310, 160);
    lv_obj_align(fmListContainer, LV_ALIGN_TOP_LEFT, 5, 36);
    ui_apply_card_style(fmListContainer);

    refresh_file_manager_drives();
}
