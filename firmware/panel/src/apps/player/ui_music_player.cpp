#include "ui_music_player.h"
#include "music_scanner.h"
#include "panel_theme.h"
#include "panel_main.h"
#include "driver/panel_audio.h"
#include "apps/player/lyrics_engine.h"
#include "micro_flac/flac_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

lv_obj_t* musicPlayerScreen = nullptr;

// Main Screen UI Elements
static lv_obj_t* actionBarContainer = nullptr;
static lv_obj_t* btnFullScanActionBar = nullptr;
static lv_obj_t* btnPartialScanActionBar = nullptr;

static lv_obj_t* playlistTabView = nullptr;
static lv_obj_t* tabTitleContainer = nullptr;
static lv_obj_t* tabArtistContainer = nullptr;
static lv_obj_t* tabAlbumContainer = nullptr;
static lv_obj_t* tabGenreContainer = nullptr;
static lv_obj_t* tabAlbArtistContainer = nullptr;

static lv_obj_t* btnCenterFullScan = nullptr;

static lv_obj_t* miniPlayerContainer = nullptr;
static lv_obj_t* miniCoverImg = nullptr;
static lv_obj_t* miniTitleLbl = nullptr;
static lv_obj_t* miniArtistLbl = nullptr;
static lv_obj_t* miniBtnPlayPause = nullptr;
static lv_obj_t* miniBtnNext = nullptr;
static lv_obj_t* miniProgressBar = nullptr;

// Scan Progress Overlay
static lv_obj_t* scanModalOverlay = nullptr;
static lv_obj_t* scanModalTitle = nullptr;
static lv_obj_t* scanModalProgress = nullptr;
static lv_obj_t* scanModalStatusLbl = nullptr;

// Full Player Overlay Elements
static lv_obj_t* fullPlayerOverlay = nullptr;
static lv_obj_t* fullCoverImg = nullptr;
static lv_obj_t* fullTitleLbl = nullptr;
static lv_obj_t* fullArtistLbl = nullptr;
static lv_obj_t* fullMetaBadge = nullptr;

// VCD Karaoke Smooth Blue Sweeping Fill Container
static lv_obj_t* karaokeCard = nullptr;
static lv_obj_t* lblBaseWhiteText = nullptr;
static lv_obj_t* overlayBlueClipContainer = nullptr;
static lv_obj_t* lblBlueFillText = nullptr;
static lv_obj_t* interludeDotLbl[3] = {nullptr, nullptr, nullptr};

enum LyricsState {
    STATE_IDLE = 0,
    STATE_LINE_ACTIVE = 1,
    STATE_INTERLUDE_BOUNCING = 2,
    STATE_INTERLUDE_COUNTDOWN = 3
};

static LyricsState currentLyricsState = STATE_IDLE;

static lv_obj_t* fullBarProgress = nullptr;
static lv_obj_t* fullLblTime = nullptr;
static lv_obj_t* fullBtnPlayPause = nullptr;
static lv_obj_t* fullBtnRepeat = nullptr;

// Full Player TabView (Tab 1: Visualizer/VU, Tab 2: Full Auto-scroll Lyrics)
static lv_obj_t* fullTabView = nullptr;

// Tab 1: Analyzer Bars & VU Meters
static const uint8_t kNumPlayerSpecBars = 10;
static lv_obj_t* fullSpecBars[10] = {nullptr};
static lv_obj_t* fullVuBarL = nullptr;
static lv_obj_t* fullVuBarR = nullptr;

// Tab 2: Auto-scrolling Full Lyrics List
static lv_obj_t* fullLyricsListContainer = nullptr;

// Playback State & Core 1 UI Task
enum RepeatMode {
    REPEAT_OFF = 0,
    REPEAT_ALL = 1,
    REPEAT_ONE = 2,
    REPEAT_SHUFFLE = 3
};

static std::vector<TrackMetadata> playlistTracks;
static int currentTrackIdx = -1;
static bool isPlaying = false;
static RepeatMode currentRepeatMode = REPEAT_OFF;
static bool isFullPlayerVisible = false;

static LyricsTrackData currentTrackLyrics;
static int currentActiveLineIdx = -1;

static TaskHandle_t ui_display_task_handle = NULL;

// Forward Declarations
static void refresh_playlist_view(void);
static void update_playback_ui(void);
static void show_scan_modal(const char* title);
static void hide_scan_modal(void);
static void update_scan_progress(size_t processed, size_t total, const std::string& current_file);
static void trigger_full_scan(void);
static void trigger_partial_scan(void);
static void play_track_at_index(int idx);
static void show_full_player(bool show);
static void render_full_lyrics_tab(const LyricsTrackData& lyrics);

static void scan_callback_bridge(size_t processed, size_t total, const std::string& current_file) {
    update_scan_progress(processed, total, current_file);
}

// Core 1 Display Render & Animation Task (Priority 5, 60 FPS)
static void ui_display_render_task(void* pvParameters) {
    while (1) {
        if (isPlaying) {
            update_music_player_screen();
        }
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS update
    }
}

void create_music_player_screen(void) {
    if (musicPlayerScreen) return;

    musicPlayerScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(musicPlayerScreen, UI_COLOR_BG, 0);

    // ==========================================
    // 1. ACTION BAR (Top Header Card)
    // ==========================================
    actionBarContainer = lv_obj_create(musicPlayerScreen);
    lv_obj_set_size(actionBarContainer, 320, 36);
    lv_obj_align(actionBarContainer, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(actionBarContainer, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(actionBarContainer, 0, 0);
    lv_obj_set_style_radius(actionBarContainer, 0, 0);
    lv_obj_set_style_pad_all(actionBarContainer, 2, 0);

    lv_obj_t* appTitle = lv_label_create(actionBarContainer);
    lv_label_set_text(appTitle, LV_SYMBOL_AUDIO " MUSIC");
    lv_obj_set_style_text_color(appTitle, UI_COLOR_PRIMARY, 0);
    lv_obj_align(appTitle, LV_ALIGN_LEFT_MID, 4, 0);

    btnFullScanActionBar = lv_btn_create(actionBarContainer);
    lv_obj_set_size(btnFullScanActionBar, 76, 26);
    lv_obj_align(btnFullScanActionBar, LV_ALIGN_RIGHT_MID, -86, 0);
    ui_apply_btn_style(btnFullScanActionBar, lv_color_hex(0x00838f));
    lv_obj_t* lblFS = lv_label_create(btnFullScanActionBar);
    lv_label_set_text(lblFS, "Full Scan");
    lv_obj_set_style_text_font(lblFS, &lv_font_montserrat_10, 0);
    lv_obj_center(lblFS);

    lv_obj_add_event_cb(btnFullScanActionBar, [](lv_event_t* e) {
        trigger_full_scan();
    }, LV_EVENT_CLICKED, NULL);

    btnPartialScanActionBar = lv_btn_create(actionBarContainer);
    lv_obj_set_size(btnPartialScanActionBar, 78, 26);
    lv_obj_align(btnPartialScanActionBar, LV_ALIGN_RIGHT_MID, -4, 0);
    ui_apply_btn_style(btnPartialScanActionBar, lv_color_hex(0x2e7d32));
    lv_obj_t* lblPS = lv_label_create(btnPartialScanActionBar);
    lv_label_set_text(lblPS, "Partial Scan");
    lv_obj_set_style_text_font(lblPS, &lv_font_montserrat_10, 0);
    lv_obj_center(lblPS);

    lv_obj_add_event_cb(btnPartialScanActionBar, [](lv_event_t* e) {
        trigger_partial_scan();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnHome = lv_btn_create(actionBarContainer);
    lv_obj_set_size(btnHome, 28, 26);
    lv_obj_align(btnHome, LV_ALIGN_LEFT_MID, 76, 0);
    ui_apply_btn_style(btnHome, UI_COLOR_SURFACE);
    lv_obj_t* lblHome = lv_label_create(btnHome);
    lv_label_set_text(lblHome, LV_SYMBOL_LEFT);
    lv_obj_center(lblHome);

    lv_obj_add_event_cb(btnHome, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::OPERATIONAL);
    }, LV_EVENT_CLICKED, NULL);

    // ==========================================
    // 2. PLAYLIST TABVIEW
    // ==========================================
    playlistTabView = lv_tabview_create(musicPlayerScreen, LV_DIR_TOP, 24);
    lv_obj_set_size(playlistTabView, 320, 156);
    lv_obj_align(playlistTabView, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(playlistTabView, UI_COLOR_BG, 0);

    tabTitleContainer = lv_tabview_add_tab(playlistTabView, "Title");
    tabArtistContainer = lv_tabview_add_tab(playlistTabView, "Artist");
    tabAlbumContainer = lv_tabview_add_tab(playlistTabView, "Album");
    tabGenreContainer = lv_tabview_add_tab(playlistTabView, "Genre");
    tabAlbArtistContainer = lv_tabview_add_tab(playlistTabView, "Alb.Artist");

    btnCenterFullScan = lv_btn_create(musicPlayerScreen);
    lv_obj_set_size(btnCenterFullScan, 180, 48);
    lv_obj_align(btnCenterFullScan, LV_ALIGN_CENTER, 0, -10);
    ui_apply_btn_style(btnCenterFullScan, lv_color_hex(0x00838f));

    lv_obj_t* lblCenterScan = lv_label_create(btnCenterFullScan);
    lv_label_set_text(lblCenterScan, LV_SYMBOL_REFRESH " FULL SCAN");
    lv_obj_center(lblCenterScan);

    lv_obj_add_event_cb(btnCenterFullScan, [](lv_event_t* e) {
        trigger_full_scan();
    }, LV_EVENT_CLICKED, NULL);

    // ==========================================
    // 3. ANDROID FLOATING MINI PLAYER BAR
    // ==========================================
    miniPlayerContainer = lv_obj_create(musicPlayerScreen);
    lv_obj_set_size(miniPlayerContainer, 320, 48);
    lv_obj_align(miniPlayerContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    ui_apply_card_style(miniPlayerContainer);
    lv_obj_set_style_radius(miniPlayerContainer, 0, 0);

    miniProgressBar = lv_bar_create(miniPlayerContainer);
    lv_obj_set_size(miniProgressBar, 320, 3);
    lv_obj_align(miniProgressBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_bar_set_range(miniProgressBar, 0, 100);
    lv_obj_set_style_bg_color(miniProgressBar, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(miniProgressBar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);

    miniCoverImg = lv_img_create(miniPlayerContainer);
    lv_obj_set_size(miniCoverImg, 36, 36);
    lv_obj_align(miniCoverImg, LV_ALIGN_LEFT_MID, 4, 2);

    miniTitleLbl = lv_label_create(miniPlayerContainer);
    lv_label_set_text(miniTitleLbl, "No Track Selected");
    lv_obj_set_style_text_color(miniTitleLbl, UI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(miniTitleLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(miniTitleLbl, LV_ALIGN_TOP_LEFT, 46, 6);

    miniArtistLbl = lv_label_create(miniPlayerContainer);
    lv_label_set_text(miniArtistLbl, "Select a track to play");
    lv_obj_set_style_text_color(miniArtistLbl, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(miniArtistLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(miniArtistLbl, LV_ALIGN_TOP_LEFT, 46, 24);

    miniBtnPlayPause = lv_btn_create(miniPlayerContainer);
    lv_obj_set_size(miniBtnPlayPause, 32, 32);
    lv_obj_align(miniBtnPlayPause, LV_ALIGN_RIGHT_MID, -42, 2);
    ui_apply_btn_style(miniBtnPlayPause, UI_COLOR_PRIMARY);
    lv_obj_t* lblMiniPP = lv_label_create(miniBtnPlayPause);
    lv_label_set_text(lblMiniPP, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(lblMiniPP, lv_color_hex(0x000000), 0);
    lv_obj_center(lblMiniPP);

    lv_obj_add_event_cb(miniBtnPlayPause, [](lv_event_t* e) {
        if (currentTrackIdx < 0 && !playlistTracks.empty()) {
            play_track_at_index(0);
        } else if (currentTrackIdx >= 0) {
            isPlaying = !isPlaying;
            update_playback_ui();
        }
    }, LV_EVENT_CLICKED, NULL);

    miniBtnNext = lv_btn_create(miniPlayerContainer);
    lv_obj_set_size(miniBtnNext, 32, 32);
    lv_obj_align(miniBtnNext, LV_ALIGN_RIGHT_MID, -6, 2);
    ui_apply_btn_style(miniBtnNext, UI_COLOR_SURFACE_LIGHT);
    lv_obj_t* lblMiniNxt = lv_label_create(miniBtnNext);
    lv_label_set_text(lblMiniNxt, LV_SYMBOL_NEXT);
    lv_obj_center(lblMiniNxt);

    lv_obj_add_event_cb(miniBtnNext, [](lv_event_t* e) {
        if (!playlistTracks.empty()) {
            int nextIdx = (currentTrackIdx + 1) % playlistTracks.size();
            play_track_at_index(nextIdx);
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(miniPlayerContainer, [](lv_event_t* e) {
        lv_obj_t* target = lv_event_get_target(e);
        if (target != miniBtnPlayPause && target != miniBtnNext) {
            show_full_player(true);
        }
    }, LV_EVENT_CLICKED, NULL);

    // ==========================================
    // 4. SCAN PROGRESS OVERLAY MODAL
    // ==========================================
    scanModalOverlay = lv_obj_create(musicPlayerScreen);
    lv_obj_set_size(scanModalOverlay, 280, 140);
    lv_obj_center(scanModalOverlay);
    ui_apply_card_style(scanModalOverlay);
    lv_obj_add_flag(scanModalOverlay, LV_OBJ_FLAG_HIDDEN);

    scanModalTitle = lv_label_create(scanModalOverlay);
    lv_label_set_text(scanModalTitle, "Scanning SD Card...");
    lv_obj_set_style_text_color(scanModalTitle, UI_COLOR_PRIMARY, 0);
    lv_obj_align(scanModalTitle, LV_ALIGN_TOP_MID, 0, 6);

    scanModalProgress = lv_bar_create(scanModalOverlay);
    lv_obj_set_size(scanModalProgress, 240, 14);
    lv_obj_align(scanModalProgress, LV_ALIGN_CENTER, 0, -6);
    lv_bar_set_range(scanModalProgress, 0, 100);
    lv_obj_set_style_bg_color(scanModalProgress, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(scanModalProgress, UI_COLOR_SECONDARY, LV_PART_INDICATOR);

    scanModalStatusLbl = lv_label_create(scanModalOverlay);
    lv_label_set_text(scanModalStatusLbl, "Preparing scan...");
    lv_obj_set_style_text_color(scanModalStatusLbl, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(scanModalStatusLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(scanModalStatusLbl, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ==========================================
    // 5. FULL PLAYER OVERLAY
    // ==========================================
    fullPlayerOverlay = lv_obj_create(musicPlayerScreen);
    lv_obj_set_size(fullPlayerOverlay, 320, 240);
    lv_obj_align(fullPlayerOverlay, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(fullPlayerOverlay, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(fullPlayerOverlay, 0, 0);
    lv_obj_set_style_pad_all(fullPlayerOverlay, 4, 0);
    lv_obj_add_flag(fullPlayerOverlay, LV_OBJ_FLAG_HIDDEN);

    // Close Button
    lv_obj_t* btnCloseFull = lv_btn_create(fullPlayerOverlay);
    lv_obj_set_size(btnCloseFull, 30, 24);
    lv_obj_align(btnCloseFull, LV_ALIGN_TOP_LEFT, 2, 2);
    ui_apply_btn_style(btnCloseFull, UI_COLOR_SURFACE_LIGHT);
    lv_obj_t* lblClose = lv_label_create(btnCloseFull);
    lv_label_set_text(lblClose, LV_SYMBOL_DOWN);
    lv_obj_center(lblClose);

    lv_obj_add_event_cb(btnCloseFull, [](lv_event_t* e) {
        show_full_player(false);
    }, LV_EVENT_CLICKED, NULL);

    // Cover Art
    fullCoverImg = lv_img_create(fullPlayerOverlay);
    lv_obj_set_size(fullCoverImg, 44, 44);
    lv_obj_align(fullCoverImg, LV_ALIGN_TOP_LEFT, 4, 28);

    // Title, Artist, Album & High-Res Audio Badge
    fullTitleLbl = lv_label_create(fullPlayerOverlay);
    lv_label_set_text(fullTitleLbl, "Track Title");
    lv_obj_set_style_text_color(fullTitleLbl, UI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(fullTitleLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(fullTitleLbl, LV_ALIGN_TOP_LEFT, 54, 26);

    fullArtistLbl = lv_label_create(fullPlayerOverlay);
    lv_label_set_text(fullArtistLbl, "Artist Name");
    lv_obj_set_style_text_color(fullArtistLbl, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(fullArtistLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(fullArtistLbl, LV_ALIGN_TOP_LEFT, 54, 42);

    fullMetaBadge = lv_label_create(fullPlayerOverlay);
    lv_label_set_text(fullMetaBadge, "FLAC 32-Bit / 384.0 kHz");
    lv_obj_set_style_text_color(fullMetaBadge, UI_COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(fullMetaBadge, &lv_font_montserrat_10, 0);
    lv_obj_align(fullMetaBadge, LV_ALIGN_TOP_LEFT, 54, 56);

    // ==========================================
    // VCD KARAOKE SMOOTH BLUE SWEEPING FILL CONTAINER
    // ==========================================
    karaokeCard = lv_obj_create(fullPlayerOverlay);
    lv_obj_set_size(karaokeCard, 312, 28);
    lv_obj_align(karaokeCard, LV_ALIGN_TOP_MID, 0, 74);
    ui_apply_card_style(karaokeCard);
    lv_obj_set_style_pad_all(karaokeCard, 0, 0);

    // Base White Text
    lblBaseWhiteText = lv_label_create(karaokeCard);
    lv_label_set_text(lblBaseWhiteText, "♪ VCD Karaoke Lyrics ♪");
    lv_obj_set_style_text_color(lblBaseWhiteText, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lblBaseWhiteText, LV_ALIGN_CENTER, 0, 0);

    // Overlay Blue Container for smooth clipping rect sweep
    overlayBlueClipContainer = lv_obj_create(karaokeCard);
    lv_obj_set_size(overlayBlueClipContainer, 0, 28);
    lv_obj_align(overlayBlueClipContainer, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(overlayBlueClipContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overlayBlueClipContainer, 0, 0);
    lv_obj_set_style_pad_all(overlayBlueClipContainer, 0, 0);
    lv_obj_set_style_clip_corner(overlayBlueClipContainer, true, 0);
    lv_obj_clear_flag(overlayBlueClipContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Smooth Blue Fill Label
    lblBlueFillText = lv_label_create(overlayBlueClipContainer);
    lv_label_set_text(lblBlueFillText, "♪ VCD Karaoke Lyrics ♪");
    lv_obj_set_style_text_color(lblBlueFillText, lv_color_hex(0x00E5FF), 0);
    lv_obj_align(lblBlueFillText, LV_ALIGN_CENTER, 0, 0);

    // 3 Bouncing Interlude Dots ("•", "•", "•")
    for (int d = 0; d < 3; d++) {
        interludeDotLbl[d] = lv_label_create(karaokeCard);
        lv_label_set_text(interludeDotLbl[d], "•");
        lv_obj_set_style_text_color(interludeDotLbl[d], UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_text_font(interludeDotLbl[d], &lv_font_montserrat_18, 0);
        lv_obj_align(interludeDotLbl[d], LV_ALIGN_CENTER, (d - 1) * 16, 0);
        lv_obj_add_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
    }

    // TABVIEW: Tab 1 (Spectrum Visualizer & Stereo VU), Tab 2 (Auto-Scrolling Full Lyrics)
    fullTabView = lv_tabview_create(fullPlayerOverlay, LV_DIR_TOP, 24);
    lv_obj_set_size(fullTabView, 312, 88);
    lv_obj_align(fullTabView, LV_ALIGN_TOP_MID, 0, 102);
    lv_obj_set_style_bg_color(fullTabView, UI_COLOR_BG, 0);

    lv_obj_t* tabVisualizer = lv_tabview_add_tab(fullTabView, "Analyzer / VU");
    lv_obj_t* tabFullLyrics = lv_tabview_add_tab(fullTabView, "Full Lyrics");

    for (uint8_t i = 0; i < kNumPlayerSpecBars; i++) {
        fullSpecBars[i] = lv_bar_create(tabVisualizer);
        lv_obj_set_size(fullSpecBars[i], 10, 42);
        lv_obj_align(fullSpecBars[i], LV_ALIGN_BOTTOM_LEFT, i * 14 + 2, 0);
        lv_bar_set_range(fullSpecBars[i], 0, 255);
        lv_bar_set_value(fullSpecBars[i], 20, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(fullSpecBars[i], lv_color_hex(0x222533), LV_PART_MAIN);
        lv_obj_set_style_bg_color(fullSpecBars[i], UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    }

    lv_obj_t* vuBox = lv_obj_create(tabVisualizer);
    lv_obj_set_size(vuBox, 140, 46);
    lv_obj_align(vuBox, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_apply_card_style(vuBox);

    fullVuBarL = lv_bar_create(vuBox);
    lv_obj_set_size(fullVuBarL, 120, 8);
    lv_obj_align(fullVuBarL, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_bar_set_range(fullVuBarL, 0, 255);
    lv_obj_set_style_bg_color(fullVuBarL, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(fullVuBarL, UI_COLOR_SECONDARY, LV_PART_INDICATOR);

    fullVuBarR = lv_bar_create(vuBox);
    lv_obj_set_size(fullVuBarR, 120, 8);
    lv_obj_align(fullVuBarR, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    lv_bar_set_range(fullVuBarR, 0, 255);
    lv_obj_set_style_bg_color(fullVuBarR, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(fullVuBarR, lv_color_hex(0xffa726), LV_PART_INDICATOR);

    fullLyricsListContainer = lv_obj_create(tabFullLyrics);
    lv_obj_set_size(fullLyricsListContainer, 290, 52);
    lv_obj_center(fullLyricsListContainer);
    lv_obj_set_style_bg_color(fullLyricsListContainer, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(fullLyricsListContainer, 0, 0);

    fullBarProgress = lv_bar_create(fullPlayerOverlay);
    lv_obj_set_size(fullBarProgress, 230, 6);
    lv_obj_align(fullBarProgress, LV_ALIGN_BOTTOM_LEFT, 6, 42);
    lv_bar_set_range(fullBarProgress, 0, 100);
    lv_obj_set_style_bg_color(fullBarProgress, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(fullBarProgress, UI_COLOR_PRIMARY, LV_PART_INDICATOR);

    fullLblTime = lv_label_create(fullPlayerOverlay);
    lv_label_set_text(fullLblTime, "00:00/00:00");
    lv_obj_set_style_text_color(fullLblTime, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(fullLblTime, &lv_font_montserrat_10, 0);
    lv_obj_align(fullLblTime, LV_ALIGN_BOTTOM_RIGHT, -6, 40);

    lv_obj_t* fullControlCard = lv_obj_create(fullPlayerOverlay);
    lv_obj_set_size(fullControlCard, 312, 36);
    lv_obj_align(fullControlCard, LV_ALIGN_BOTTOM_MID, 0, -2);
    ui_apply_card_style(fullControlCard);

    fullBtnRepeat = lv_btn_create(fullControlCard);
    lv_obj_set_size(fullBtnRepeat, 36, 28);
    lv_obj_align(fullBtnRepeat, LV_ALIGN_LEFT_MID, 4, 0);
    ui_apply_btn_style(fullBtnRepeat, UI_COLOR_SURFACE_LIGHT);
    lv_obj_t* lblRep = lv_label_create(fullBtnRepeat);
    lv_label_set_text(lblRep, LV_SYMBOL_LOOP);
    lv_obj_center(lblRep);

    lv_obj_add_event_cb(fullBtnRepeat, [](lv_event_t* e) {
        currentRepeatMode = (RepeatMode)((currentRepeatMode + 1) % 4);
        update_playback_ui();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnPrev = lv_btn_create(fullControlCard);
    lv_obj_set_size(btnPrev, 44, 28);
    lv_obj_align(btnPrev, LV_ALIGN_LEFT_MID, 52, 0);
    ui_apply_btn_style(btnPrev, UI_COLOR_SURFACE_LIGHT);
    lv_obj_t* lblPrev = lv_label_create(btnPrev);
    lv_label_set_text(lblPrev, LV_SYMBOL_PREV);
    lv_obj_center(lblPrev);

    lv_obj_add_event_cb(btnPrev, [](lv_event_t* e) {
        if (!playlistTracks.empty()) {
            int prevIdx = (currentTrackIdx > 0) ? currentTrackIdx - 1 : (int)playlistTracks.size() - 1;
            play_track_at_index(prevIdx);
        }
    }, LV_EVENT_CLICKED, NULL);

    fullBtnPlayPause = lv_btn_create(fullControlCard);
    lv_obj_set_size(fullBtnPlayPause, 50, 28);
    lv_obj_align(fullBtnPlayPause, LV_ALIGN_CENTER, 0, 0);
    ui_apply_btn_style(fullBtnPlayPause, UI_COLOR_PRIMARY);
    lv_obj_t* lblFullPP = lv_label_create(fullBtnPlayPause);
    lv_label_set_text(lblFullPP, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(lblFullPP, lv_color_hex(0x000000), 0);
    lv_obj_center(lblFullPP);

    lv_obj_add_event_cb(fullBtnPlayPause, [](lv_event_t* e) {
        if (currentTrackIdx < 0 && !playlistTracks.empty()) {
            play_track_at_index(0);
        } else if (currentTrackIdx >= 0) {
            isPlaying = !isPlaying;
            update_playback_ui();
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnNext = lv_btn_create(fullControlCard);
    lv_obj_set_size(btnNext, 44, 28);
    lv_obj_align(btnNext, LV_ALIGN_RIGHT_MID, -52, 0);
    ui_apply_btn_style(btnNext, UI_COLOR_SURFACE_LIGHT);
    lv_obj_t* lblNext = lv_label_create(btnNext);
    lv_label_set_text(lblNext, LV_SYMBOL_NEXT);
    lv_obj_center(lblNext);

    lv_obj_add_event_cb(btnNext, [](lv_event_t* e) {
        if (!playlistTracks.empty()) {
            int nextIdx = (currentTrackIdx + 1) % playlistTracks.size();
            play_track_at_index(nextIdx);
        }
    }, LV_EVENT_CLICKED, NULL);

    // Start Core 1 UI Display Render Task (Priority 5)
    if (ui_display_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            ui_display_render_task,
            "ui_display",
            4096,
            NULL,
            5,
            &ui_display_task_handle,
            1 // Core 1
        );
    }

    playlistTracks = MusicScanner::load_library();
    refresh_playlist_view();
    update_playback_ui();
}

static void render_track_list_into_container(lv_obj_t* container, const std::vector<size_t>& track_indices, const std::string& group_header) {
    if (!container) return;
    lv_obj_clean(container);

    if (!group_header.empty()) {
        lv_obj_t* headerLbl = lv_label_create(container);
        lv_label_set_text(headerLbl, group_header.c_str());
        lv_obj_set_style_text_color(headerLbl, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_text_font(headerLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(headerLbl, LV_ALIGN_TOP_LEFT, 2, 0);
    }

    int yOffset = group_header.empty() ? 0 : 16;
    for (size_t i = 0; i < track_indices.size(); i++) {
        size_t origIdx = track_indices[i];
        const TrackMetadata& track = playlistTracks[origIdx];

        lv_obj_t* itemBtn = lv_btn_create(container);
        lv_obj_set_size(itemBtn, 290, 32);
        lv_obj_align(itemBtn, LV_ALIGN_TOP_MID, 0, yOffset + (int)i * 36);

        if ((int)origIdx == currentTrackIdx) {
            ui_apply_btn_style(itemBtn, lv_color_hex(0x00838f));
        } else {
            ui_apply_btn_style(itemBtn, UI_COLOR_SURFACE);
        }

        lv_obj_t* icon = lv_label_create(itemBtn);
        lv_label_set_text(icon, ((int)origIdx == currentTrackIdx && isPlaying) ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(icon, UI_COLOR_PRIMARY, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t* titleLbl = lv_label_create(itemBtn);
        lv_label_set_text(titleLbl, track.title.c_str());
        lv_obj_set_style_text_color(titleLbl, UI_COLOR_TEXT_MAIN, 0);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 26, 0);

        lv_obj_t* metaLbl = lv_label_create(itemBtn);
        char mbuf[80];
        snprintf(mbuf, sizeof(mbuf), "%s • %s", track.artist.c_str(), track.album.c_str());
        lv_label_set_text(metaLbl, mbuf);
        lv_obj_set_style_text_color(metaLbl, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(metaLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(metaLbl, LV_ALIGN_BOTTOM_LEFT, 26, -2);

        int trackIndex = (int)origIdx;
        lv_obj_add_event_cb(itemBtn, [](lv_event_t* e) {
            int idx = (int)(uintptr_t)lv_event_get_user_data(e);
            play_track_at_index(idx);
        }, LV_EVENT_CLICKED, (void*)(uintptr_t)trackIndex);
    }
}

static void refresh_playlist_view(void) {
    if (playlistTracks.empty()) {
        lv_obj_clear_flag(btnCenterFullScan, LV_OBJ_FLAG_HIDDEN);
        if (tabTitleContainer) lv_obj_clean(tabTitleContainer);
        if (tabArtistContainer) lv_obj_clean(tabArtistContainer);
        if (tabAlbumContainer) lv_obj_clean(tabAlbumContainer);
        if (tabGenreContainer) lv_obj_clean(tabGenreContainer);
        if (tabAlbArtistContainer) lv_obj_clean(tabAlbArtistContainer);
        return;
    }

    lv_obj_add_flag(btnCenterFullScan, LV_OBJ_FLAG_HIDDEN);

    std::vector<size_t> titleIndices(playlistTracks.size());
    for (size_t i = 0; i < playlistTracks.size(); i++) titleIndices[i] = i;
    std::sort(titleIndices.begin(), titleIndices.end(), [](size_t a, size_t b) {
        return playlistTracks[a].title < playlistTracks[b].title;
    });
    render_track_list_into_container(tabTitleContainer, titleIndices, "All Tracks (By Title)");

    std::vector<size_t> artistIndices(playlistTracks.size());
    for (size_t i = 0; i < playlistTracks.size(); i++) artistIndices[i] = i;
    std::sort(artistIndices.begin(), artistIndices.end(), [](size_t a, size_t b) {
        if (playlistTracks[a].artist != playlistTracks[b].artist) return playlistTracks[a].artist < playlistTracks[b].artist;
        return playlistTracks[a].title < playlistTracks[b].title;
    });
    render_track_list_into_container(tabArtistContainer, artistIndices, "Tracks Categorized by Artist");

    std::vector<size_t> albumIndices(playlistTracks.size());
    for (size_t i = 0; i < playlistTracks.size(); i++) albumIndices[i] = i;
    std::sort(albumIndices.begin(), albumIndices.end(), [](size_t a, size_t b) {
        if (playlistTracks[a].album != playlistTracks[b].album) return playlistTracks[a].album < playlistTracks[b].album;
        if (playlistTracks[a].track_num != playlistTracks[b].track_num) return playlistTracks[a].track_num < playlistTracks[b].track_num;
        return playlistTracks[a].title < playlistTracks[b].title;
    });
    render_track_list_into_container(tabAlbumContainer, albumIndices, "Tracks Categorized by Album");

    std::vector<size_t> genreIndices(playlistTracks.size());
    for (size_t i = 0; i < playlistTracks.size(); i++) genreIndices[i] = i;
    std::sort(genreIndices.begin(), genreIndices.end(), [](size_t a, size_t b) {
        if (playlistTracks[a].genre != playlistTracks[b].genre) return playlistTracks[a].genre < playlistTracks[b].genre;
        return playlistTracks[a].title < playlistTracks[b].title;
    });
    render_track_list_into_container(tabGenreContainer, genreIndices, "Tracks Categorized by Genre");

    std::vector<size_t> albArtistIndices(playlistTracks.size());
    for (size_t i = 0; i < playlistTracks.size(); i++) albArtistIndices[i] = i;
    std::sort(albArtistIndices.begin(), albArtistIndices.end(), [](size_t a, size_t b) {
        if (playlistTracks[a].album_artist != playlistTracks[b].album_artist) return playlistTracks[a].album_artist < playlistTracks[b].album_artist;
        if (playlistTracks[a].album != playlistTracks[b].album) return playlistTracks[a].album < playlistTracks[b].album;
        return playlistTracks[a].title < playlistTracks[b].title;
    });
    render_track_list_into_container(tabAlbArtistContainer, albArtistIndices, "Tracks Categorized by Album Artist");
}

static void play_track_at_index(int idx) {
    if (idx < 0 || idx >= (int)playlistTracks.size()) return;
    currentTrackIdx = idx;
    panel_audio_set_position_ms(0);
    isPlaying = true;

    const TrackMetadata& track = playlistTracks[currentTrackIdx];

    // Priority Fallback Discovery for Lyrics
    currentTrackLyrics = LyricsEngine::discover_and_parse(
        track.full_path,
        track.embedded_lyrics_synced,
        track.embedded_lyrics_unsynced
    );
    currentActiveLineIdx = -1;

    panel_send_log("info", ("Playing track " + track.title + " (Lyrics Mode: " + std::to_string(currentTrackLyrics.mode) + ")").c_str());

    refresh_playlist_view();
    update_playback_ui();
    render_full_lyrics_tab(currentTrackLyrics);
}

static void show_full_player(bool show) {
    isFullPlayerVisible = show;
    if (show) {
        lv_obj_clear_flag(fullPlayerOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(fullPlayerOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_scan_modal(const char* title) {
    if (scanModalOverlay) {
        if (scanModalTitle) lv_label_set_text(scanModalTitle, title);
        if (scanModalProgress) lv_bar_set_value(scanModalProgress, 0, LV_ANIM_OFF);
        if (scanModalStatusLbl) lv_label_set_text(scanModalStatusLbl, "Initializing scan...");
        lv_obj_clear_flag(scanModalOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_scan_modal(void) {
    if (scanModalOverlay) {
        lv_obj_add_flag(scanModalOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_scan_progress(size_t processed, size_t total, const std::string& current_file) {
    if (!scanModalOverlay) return;

    if (total > 0 && scanModalProgress) {
        int pct = (int)(processed * 100 / total);
        lv_bar_set_value(scanModalProgress, pct, LV_ANIM_OFF);
    }

    if (scanModalStatusLbl) {
        char sbuf[128];
        size_t slash = current_file.find_last_of('/');
        std::string fname = (slash != std::string::npos) ? current_file.substr(slash + 1) : current_file;
        snprintf(sbuf, sizeof(sbuf), "(%d/%d) %s", (int)processed, (int)total, fname.c_str());
        lv_label_set_text(scanModalStatusLbl, sbuf);
    }

    lv_refr_now(NULL);
}

static void trigger_full_scan(void) {
    show_scan_modal("Full Scan in Progress");
    playlistTracks = MusicScanner::full_scan(scan_callback_bridge);
    hide_scan_modal();
    currentTrackIdx = -1;
    refresh_playlist_view();
    update_playback_ui();
}

static void trigger_partial_scan(void) {
    show_scan_modal("Partial Scan in Progress");
    playlistTracks = MusicScanner::partial_scan(scan_callback_bridge);
    hide_scan_modal();
    refresh_playlist_view();
    update_playback_ui();
}

static void render_full_lyrics_tab(const LyricsTrackData& lyrics) {
    if (!fullLyricsListContainer) return;
    lv_obj_clean(fullLyricsListContainer);

    if (!lyrics.is_valid()) {
        lv_obj_t* emptyLbl = lv_label_create(fullLyricsListContainer);
        lv_label_set_text(emptyLbl, "♪ Lyrics Unsupported ♪");
        lv_obj_set_style_text_color(emptyLbl, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_center(emptyLbl);
    } else {
        for (size_t i = 0; i < lyrics.lines.size(); i++) {
            lv_obj_t* lineLbl = lv_label_create(fullLyricsListContainer);
            lv_label_set_text(lineLbl, lyrics.lines[i].full_text.c_str());
            lv_obj_set_style_text_color(lineLbl, UI_COLOR_TEXT_MAIN, 0);
            lv_obj_set_style_text_font(lineLbl, &lv_font_montserrat_10, 0);
            lv_obj_align(lineLbl, LV_ALIGN_TOP_LEFT, 2, (int)i * 16);
        }
    }
}

static void update_playback_ui(void) {
    if (currentTrackIdx < 0 || currentTrackIdx >= (int)playlistTracks.size()) {
        if (miniTitleLbl) lv_label_set_text(miniTitleLbl, "No Track Selected");
        if (miniArtistLbl) lv_label_set_text(miniArtistLbl, "Select a track to play");
        if (miniProgressBar) lv_bar_set_value(miniProgressBar, 0, LV_ANIM_OFF);
        return;
    }

    const TrackMetadata& track = playlistTracks[currentTrackIdx];

    if (miniTitleLbl) lv_label_set_text(miniTitleLbl, track.title.c_str());
    if (miniArtistLbl) lv_label_set_text(miniArtistLbl, track.artist.c_str());

    lv_obj_t* lblMiniPP = lv_obj_get_child(miniBtnPlayPause, 0);
    if (lblMiniPP) lv_label_set_text(lblMiniPP, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if (fullTitleLbl) lv_label_set_text(fullTitleLbl, track.title.c_str());
    if (fullArtistLbl) lv_label_set_text(fullArtistLbl, track.artist.c_str());

    if (fullMetaBadge) {
        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "FLAC %u-Bit / %.1f kHz", track.bit_depth, (float)track.sample_rate / 1000.0f);
        lv_label_set_text(fullMetaBadge, mbuf);
    }

    lv_obj_t* lblFullPP = lv_obj_get_child(fullBtnPlayPause, 0);
    if (lblFullPP) lv_label_set_text(lblFullPP, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    lv_obj_t* lblRep = lv_obj_get_child(fullBtnRepeat, 0);
    if (lblRep) {
        if (currentRepeatMode == REPEAT_OFF) {
            ui_apply_btn_style(fullBtnRepeat, UI_COLOR_SURFACE_LIGHT);
            lv_label_set_text(lblRep, LV_SYMBOL_LOOP);
        } else if (currentRepeatMode == REPEAT_ALL) {
            ui_apply_btn_style(fullBtnRepeat, lv_color_hex(0x00838f));
            lv_label_set_text(lblRep, LV_SYMBOL_LOOP);
        } else if (currentRepeatMode == REPEAT_ONE) {
            ui_apply_btn_style(fullBtnRepeat, lv_color_hex(0x2e7d32));
            lv_label_set_text(lblRep, LV_SYMBOL_REFRESH);
        } else if (currentRepeatMode == REPEAT_SHUFFLE) {
            ui_apply_btn_style(fullBtnRepeat, UI_COLOR_ACCENT_PURPLE);
            lv_label_set_text(lblRep, LV_SYMBOL_SHUFFLE);
        }
    }
}

void update_music_player_screen(void) {
    if (!musicPlayerScreen || currentTrackIdx < 0 || currentTrackIdx >= (int)playlistTracks.size()) return;

    const TrackMetadata& track = playlistTracks[currentTrackIdx];
    uint32_t currentProgressMs = panel_audio_get_position_ms();

    if (isPlaying) {
        uint32_t maxDurMs = track.duration_sec * 1000;
        if (maxDurMs > 0 && currentProgressMs >= maxDurMs) {
            if (currentRepeatMode == REPEAT_ONE) {
                panel_audio_set_position_ms(0);
            } else if (currentRepeatMode == REPEAT_ALL) {
                play_track_at_index((currentTrackIdx + 1) % playlistTracks.size());
            } else if (currentRepeatMode == REPEAT_SHUFFLE) {
                play_track_at_index(rand() % playlistTracks.size());
            } else {
                if (currentTrackIdx + 1 < (int)playlistTracks.size()) {
                    play_track_at_index(currentTrackIdx + 1);
                } else {
                    isPlaying = false;
                    panel_audio_set_position_ms(0);
                    update_playback_ui();
                }
            }
        }
    }

    uint32_t durSec = track.duration_sec;
    uint32_t curSec = currentProgressMs / 1000;
    int pct = (durSec > 0) ? (curSec * 100 / durSec) : 0;

    if (miniProgressBar) lv_bar_set_value(miniProgressBar, pct, LV_ANIM_OFF);
    if (fullBarProgress) lv_bar_set_value(fullBarProgress, pct, LV_ANIM_OFF);

    if (fullLblTime) {
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu/%02lu:%02lu", (unsigned long)(curSec / 60), (unsigned long)(curSec % 60), (unsigned long)(durSec / 60), (unsigned long)(durSec % 60));
        lv_label_set_text(fullLblTime, tbuf);
    }

    // ==========================================
    // VCD KARAOKE SMOOTH BLUE SWEEPING & INTERLUDE RENDERER (Core 1 LCD TFT)
    // ==========================================
    if (lblBaseWhiteText && lblBlueFillText && overlayBlueClipContainer) {
        if (!currentTrackLyrics.is_valid()) {
            currentLyricsState = STATE_IDLE;
            lv_label_set_text(lblBaseWhiteText, "♪ Lyrics Unsupported ♪");
            lv_label_set_text(lblBlueFillText, "♪ Lyrics Unsupported ♪");
            lv_obj_set_width(overlayBlueClipContainer, 0);
            for (int d = 0; d < 3; d++) {
                if (interludeDotLbl[d]) lv_obj_add_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            int activeLineIdx = LyricsEngine::find_active_line_index(currentTrackLyrics, currentProgressMs);
            const auto& lines = currentTrackLyrics.lines;

            // Determine previous line end time and next line start time to calculate interlude gap
            uint32_t prevEndMs = 0;
            uint32_t nextStartMs = 0;
            bool isInterludeGap = false;

            if (activeLineIdx < 0) {
                // Before the first line
                prevEndMs = 0;
                nextStartMs = lines[0].start_ms;
                if (nextStartMs > 4000 && currentProgressMs < nextStartMs) {
                    isInterludeGap = true;
                }
            } else {
                const LyricsLine& activeLine = lines[activeLineIdx];
                if (currentProgressMs >= activeLine.start_ms && currentProgressMs < activeLine.end_ms) {
                    // Currently inside an active line
                    isInterludeGap = false;
                } else {
                    // Past activeLine.end_ms, check gap to next line
                    if (activeLineIdx + 1 < (int)lines.size()) {
                        prevEndMs = activeLine.end_ms;
                        nextStartMs = lines[activeLineIdx + 1].start_ms;
                        if (nextStartMs > prevEndMs && (nextStartMs - prevEndMs) > 4000 &&
                            currentProgressMs >= prevEndMs && currentProgressMs < nextStartMs) {
                            isInterludeGap = true;
                        }
                    }
                }
            }

            if (isInterludeGap) {
                uint32_t timeUntilNextMs = (currentProgressMs < nextStartMs) ? (nextStartMs - currentProgressMs) : 0;

                if (timeUntilNextMs <= 2000) {
                    currentLyricsState = STATE_INTERLUDE_COUNTDOWN;
                } else {
                    currentLyricsState = STATE_INTERLUDE_BOUNCING;
                }

                // Hide lyric text labels
                lv_obj_add_flag(lblBaseWhiteText, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_width(overlayBlueClipContainer, 0);

                if (currentLyricsState == STATE_INTERLUDE_BOUNCING) {
                    // Wave bouncing animation using audio position ms
                    for (int d = 0; d < 3; d++) {
                        if (interludeDotLbl[d]) {
                            lv_obj_clear_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
                            float phase = (currentProgressMs * 0.008f) - (d * 0.8f);
                            int yOffset = (int)(sinf(phase) * 5.0f);
                            lv_obj_align(interludeDotLbl[d], LV_ALIGN_CENTER, (d - 1) * 16, yOffset);
                        }
                    }
                } else { // STATE_INTERLUDE_COUNTDOWN
                    // 3 dots -> 2 dots -> 1 dot -> 0 dots
                    // interval 2000ms divided into 3 equal steps (~666ms per dot)
                    int activeDots = (int)ceilf((float)timeUntilNextMs / (2000.0f / 3.0f));
                    if (activeDots < 0) activeDots = 0;
                    if (activeDots > 3) activeDots = 3;

                    for (int d = 0; d < 3; d++) {
                        if (interludeDotLbl[d]) {
                            if (d < activeDots) {
                                lv_obj_clear_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
                                lv_obj_align(interludeDotLbl[d], LV_ALIGN_CENTER, (d - 1) * 16, 0); // stationary
                            } else {
                                lv_obj_add_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
                            }
                        }
                    }
                }
            } else if (activeLineIdx >= 0 && activeLineIdx < (int)lines.size()) {
                currentLyricsState = STATE_LINE_ACTIVE;

                // Hide interlude dots
                for (int d = 0; d < 3; d++) {
                    if (interludeDotLbl[d]) lv_obj_add_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
                }

                // Show lyric text labels
                lv_obj_clear_flag(lblBaseWhiteText, LV_OBJ_FLAG_HIDDEN);

                const LyricsLine& line = lines[activeLineIdx];
                lv_label_set_text(lblBaseWhiteText, line.full_text.c_str());

                if (currentTrackLyrics.mode == LYRICS_MODE_TTML_KARAOKE) {
                    lv_label_set_text(lblBlueFillText, line.full_text.c_str());
                    float lineRatio = LyricsEngine::get_line_fill_ratio(line, currentProgressMs);
                    int totalWidth = lv_obj_get_width(lblBaseWhiteText);
                    if (totalWidth <= 0) totalWidth = 300;
                    int overlayWidth = (int)(totalWidth * lineRatio);
                    lv_obj_set_width(overlayBlueClipContainer, overlayWidth);
                } else {
                    // LYRICS_MODE_LINE_STANDARD: No karaoke blue progress fill
                    lv_obj_set_width(overlayBlueClipContainer, 0);
                }

                // Auto-scroll lyrics tab if visible
                if (fullLyricsListContainer && activeLineIdx != currentActiveLineIdx) {
                    currentActiveLineIdx = activeLineIdx;
                    uint32_t childCount = lv_obj_get_child_cnt(fullLyricsListContainer);
                    for (uint32_t c = 0; c < childCount; c++) {
                        lv_obj_t* child = lv_obj_get_child(fullLyricsListContainer, c);
                        if (c == (uint32_t)activeLineIdx) {
                            lv_obj_set_style_text_color(child, UI_COLOR_PRIMARY, 0);
                            lv_obj_scroll_to_view(child, LV_ANIM_ON);
                        } else {
                            lv_obj_set_style_text_color(child, UI_COLOR_TEXT_MUTED, 0);
                        }
                    }
                }
            } else {
                currentLyricsState = STATE_IDLE;
                lv_obj_clear_flag(lblBaseWhiteText, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lblBaseWhiteText, "♪ Instrumental ♪");
                lv_obj_set_width(overlayBlueClipContainer, 0);
                for (int d = 0; d < 3; d++) {
                    if (interludeDotLbl[d]) lv_obj_add_flag(interludeDotLbl[d], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    bool hasActiveLink = isUartLinkOk || isIndependentBypassMode;
    for (uint8_t i = 0; i < kNumPlayerSpecBars; i++) {
        if (fullSpecBars[i]) {
            uint8_t val = (hasActiveLink && i < telemetryBandsLen) ? telemetryBands[i] : 0;
            lv_bar_set_value(fullSpecBars[i], val, LV_ANIM_OFF);
        }
    }
    if (fullVuBarL) lv_bar_set_value(fullVuBarL, hasActiveLink ? telemetryVuL : 0, LV_ANIM_OFF);
    if (fullVuBarR) lv_bar_set_value(fullVuBarR, hasActiveLink ? telemetryVuR : 0, LV_ANIM_OFF);
}
