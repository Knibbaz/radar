// Customer-feedback kiosk view: LOGO -> CHOICE -> POP -> QR -> LOGO.
//
// State machine built on LVGL v8. `init()` builds all state surfaces once as
// children of a circular-clipped root container; transitions just show/hide
// them. All timing uses LVGL timers — no busy loops.

#include "feedback_view.h"
#include "feedback_log.h"
#include "config.h"

// Audio ping (device only). The native SDL simulator doesn't link audio.cpp.
#if defined(ESP_PLATFORM)
#  include "audio.h"
#  include <SPIFFS.h>
#endif

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Log: Serial on device, stderr on sim.
#if defined(ESP_PLATFORM)
#  include <Arduino.h>
#  define FB_LOG(...) Serial.printf(__VA_ARGS__)
#else
#  define FB_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

// Colors
#define COL_GREEN lv_color_hex(0x1DFF86)
#define COL_RED   lv_color_hex(0xFF5A3C)
#define COL_INK   lv_color_hex(0xEAFFF3)
#define COL_SOFT  lv_color_hex(0x9AFFC8)
#define COL_DIM   lv_color_hex(0x5F7A6C)
#define COL_BG    lv_color_hex(0x04140B)

// =============================================================================
// State machine
// =============================================================================
enum FbState { ST_LOGO, ST_CHOICE, ST_POP, ST_QR, ST_COOLDOWN };

namespace {
struct S {
    FbState          st          = ST_LOGO;
    FeedbackAnswer   lastAnswer  = FEEDBACK_GOOD;
    int              answerCount = 0;       // count rapid taps for internal use

    // Surfaces (children of circular-clipped root)
    lv_obj_t *root       = nullptr;
    lv_obj_t *v_logo     = nullptr;   // LOGO screen
    lv_obj_t *v_choice   = nullptr;   // CHOICE screen (green/red split)
    lv_obj_t *v_pop      = nullptr;   // POP expanding circle container
    lv_obj_t *v_qr       = nullptr;   // QR screen

    // LOGO sub-elements
    lv_obj_t *logo_img   = nullptr;   // LVGL image object (or label placeholder)
    lv_obj_t *logo_placeholder = nullptr; // large centered text when no logo
    lv_timer_t *t_drift  = nullptr;   // logo drift timer
    int        driftOffX = 0;
    int        driftOffY = 0;

    // CHOICE sub-elements
    lv_obj_t *choice_left  = nullptr;  // green half
    lv_obj_t *choice_right = nullptr;  // red half
    lv_timer_t *t_choice   = nullptr;  // 10s timeout back to LOGO

    // POP animation
    lv_anim_t  pop_anim;
    lv_obj_t  *pop_circle = nullptr;
    lv_timer_t *t_pop     = nullptr;   // fallback timer if anim doesn't fire ready

    // QR
    lv_obj_t *qr_widget   = nullptr;
    lv_timer_t *t_qr      = nullptr;   // 12s auto-return

    // Cooldown — absorb taps silently after returning to LOGO
    bool       cooldownActive = false;
    lv_timer_t *t_cooldown = nullptr;

    // Live-tunable settings
    int mode            = 0;          // 0=REVIEW, 1=DASHBOARD
    int cooldownMs      = FEEDBACK_COOLDOWN_MS;
    int idleBrightness  = FEEDBACK_IDLE_BRIGHTNESS;
    char urlReview[128] = {0};        // for QR widget re-render

    // Admin overlay (LOGO long-press)
    lv_obj_t *v_admin     = nullptr;
    lv_obj_t *lbl_today   = nullptr;
    lv_obj_t *lbl_ip      = nullptr;
    lv_obj_t *lbl_url     = nullptr;
    lv_timer_t *t_admin   = nullptr;

    // Touch debounce — prevent double-tap on CHOICE
    bool tapped = false;
} s;
} // anonymous namespace

// =============================================================================
// Forward declarations
// =============================================================================
static void goto_logo(void);
static void goto_choice(void);
static void goto_pop(FeedbackAnswer ans, lv_coord_t tapX, lv_coord_t tapY);
static void goto_qr(void);
static void goto_logo_with_cooldown(void);

// =============================================================================
// Helper: make a smiley face on a colored background
// =============================================================================
static lv_obj_t *make_smiley(lv_obj_t *parent, FeedbackAnswer ans,
                              lv_coord_t hit, lv_coord_t dia) {
    // hit box
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, hit, hit);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(btn, (void *)(intptr_t)ans);

    // eyes (two small dark dots symmetric around center)
    const lv_coord_t eyeOffX = dia / 5;
    const lv_coord_t eyeOffY = -dia / 6;
    const lv_coord_t eyeS    = dia / 6;
    for (int side = -1; side <= 1; side += 2) {
        lv_obj_t *e = lv_obj_create(btn);
        lv_obj_remove_style_all(e);
        lv_obj_set_size(e, eyeS, eyeS);
        lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(e, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        lv_obj_align(e, LV_ALIGN_CENTER, side * eyeOffX, eyeOffY);
        lv_obj_clear_flag(e, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    // mouth: arc segment. Good = smile (U, angles 30..150), Bad = frown (^, 210..330)
    lv_obj_t *m = lv_arc_create(btn);
    lv_obj_set_size(m, dia * 3 / 4, dia * 3 / 4);
    lv_obj_align(m, LV_ALIGN_CENTER, 0, dia / 8);
    lv_obj_set_style_arc_opa(m, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(m, dia / 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(m, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(m, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_angles(m, (ans == FEEDBACK_GOOD) ? 30 : 210,
                          (ans == FEEDBACK_GOOD) ? 150 : 330);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return btn;
}

// =============================================================================
// State transitions
// =============================================================================

// -- LOGO (dim idle screen) ---------------------------------------------------
static void logo_drift_cb(lv_timer_t *) {
    if (!s.logo_img && !s.logo_placeholder) return;
    // Random offset +-FEEDBACK_LOGO_DRIFT_PX
    s.driftOffX = (rand() % (FEEDBACK_LOGO_DRIFT_PX * 2 + 1)) - FEEDBACK_LOGO_DRIFT_PX;
    s.driftOffY = (rand() % (FEEDBACK_LOGO_DRIFT_PX * 2 + 1)) - FEEDBACK_LOGO_DRIFT_PX;
    if (s.logo_img) {
        lv_obj_align(s.logo_img, LV_ALIGN_CENTER, s.driftOffX, s.driftOffY);
    }
    if (s.logo_placeholder) {
        lv_obj_align(s.logo_placeholder, LV_ALIGN_CENTER, s.driftOffX, s.driftOffY);
    }
}

static void logo_tap_cb(lv_event_t *) {
    // Any touch on LOGO -> exit cooldown, go to CHOICE at full brightness
    if (s.cooldownActive) return;  // still in post-QR absorb window
    // Signal main loop to restore full brightness via touch callback
    if (s.st == ST_LOGO) goto_choice();
}

static void goto_logo(void) {
    s.st = ST_LOGO;
    s.tapped = false;
    // Hide all other surfaces
    if (s.v_choice) lv_obj_add_flag(s.v_choice, LV_OBJ_FLAG_HIDDEN);
    if (s.v_pop)    lv_obj_add_flag(s.v_pop, LV_OBJ_FLAG_HIDDEN);
    if (s.v_qr)     lv_obj_add_flag(s.v_qr, LV_OBJ_FLAG_HIDDEN);
    if (s.v_logo)   lv_obj_clear_flag(s.v_logo, LV_OBJ_FLAG_HIDDEN);
    // Show HUD on LOGO
    extern void ui_set_hud_visible(bool);
    ui_set_hud_visible(true);
    // Drift timer
    if (s.t_drift) lv_timer_resume(s.t_drift);
}

// -- CHOICE (green/red split) -------------------------------------------------
static void choice_timeout_cb(lv_timer_t *) {
    if (s.st != ST_CHOICE) return;
    FB_LOG("[fb] choice timeout -> LOGO (no answer)\n");
    goto_logo();
}

static void choice_tap_cb(lv_event_t *e) {
    if (s.st != ST_CHOICE || s.tapped) return;
    s.tapped = true;
    lv_obj_t *target = lv_event_get_target(e);
    FeedbackAnswer ans = (FeedbackAnswer)(intptr_t)lv_obj_get_user_data(target);
    s.lastAnswer = ans;

    // Log answer at tap time. Map: FEEDBACK_BAD(1) -> log encoding 2 for backward compat.
    feedback_log::record((uint8_t)(ans == FEEDBACK_BAD ? 2 : 0));
    FB_LOG("[fb] %s recorded\n", (ans == FEEDBACK_GOOD) ? "GOOD" : "BAD");

    // Soft ping
#if defined(ESP_PLATFORM)
    audio_play(AUDIO_NEW);
#endif

    // Cancel the 10s timeout timer
    if (s.t_choice) {
        lv_timer_del(s.t_choice);
        s.t_choice = nullptr;
    }

    // Get tap coordinates for POP animation origin
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_next(nullptr), &point);

    goto_pop(ans, point.x, point.y);
}

static void goto_choice(void) {
    if (s.t_choice) { lv_timer_del(s.t_choice); s.t_choice = nullptr; }
    s.st = ST_CHOICE;
    s.tapped = false;
    if (s.v_logo)   lv_obj_add_flag(s.v_logo, LV_OBJ_FLAG_HIDDEN);
    if (s.v_pop)    lv_obj_add_flag(s.v_pop, LV_OBJ_FLAG_HIDDEN);
    if (s.v_qr)     lv_obj_add_flag(s.v_qr, LV_OBJ_FLAG_HIDDEN);
    if (s.v_choice) lv_obj_clear_flag(s.v_choice, LV_OBJ_FLAG_HIDDEN);
    // Hide HUD on CHOICE
    extern void ui_set_hud_visible(bool);
    ui_set_hud_visible(false);
    // Pause drift timer
    if (s.t_drift) lv_timer_pause(s.t_drift);
    // 10s timeout
    s.t_choice = lv_timer_create(choice_timeout_cb, FEEDBACK_CHOICE_TIMEOUT_MS, nullptr);
    lv_timer_set_repeat_count(s.t_choice, 1);
}

// -- POP (expanding circle) ---------------------------------------------------
static void pop_anim_exec(void *obj, int32_t v) {
    lv_obj_set_size((lv_obj_t *)obj, (lv_coord_t)v, (lv_coord_t)v);
}

static void pop_anim_ready(lv_anim_t *) {
    // POP done -> transition to QR or LOGO
    if (s.mode == 1 /* DASHBOARD */) {
        goto_logo();
    } else {
        goto_qr();
    }
}

static void goto_pop(FeedbackAnswer ans, lv_coord_t tapX, lv_coord_t tapY) {
    s.st = ST_POP;
    if (s.v_logo)   lv_obj_add_flag(s.v_logo, LV_OBJ_FLAG_HIDDEN);
    if (s.v_choice) lv_obj_add_flag(s.v_choice, LV_OBJ_FLAG_HIDDEN);
    if (s.v_qr)     lv_obj_add_flag(s.v_qr, LV_OBJ_FLAG_HIDDEN);
    if (s.v_pop)    lv_obj_clear_flag(s.v_pop, LV_OBJ_FLAG_HIDDEN);
    // HUD stays hidden during POP
    extern void ui_set_hud_visible(bool);
    ui_set_hud_visible(false);

    // Configure the expanding circle
    lv_color_t color = (ans == FEEDBACK_GOOD) ? COL_GREEN : COL_RED;
    if (!s.pop_circle) {
        s.pop_circle = lv_obj_create(s.v_pop);
        lv_obj_remove_style_all(s.pop_circle);
        lv_obj_set_style_radius(s.pop_circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s.pop_circle, color, 0);
        lv_obj_set_style_bg_opa(s.pop_circle, LV_OPA_COVER, 0);
        lv_obj_clear_flag(s.pop_circle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_set_style_bg_color(s.pop_circle, color, 0);
    }
    // Position at tap point, start size 0
    lv_obj_set_pos(s.pop_circle, (lv_coord_t)tapX, (lv_coord_t)tapY);
    lv_obj_set_size(s.pop_circle, 0, 0);

    // Animate to cover screen: diagonal ~660px to guarantee full coverage
    const int maxSize = (int)(SCREEN_W * 1.5f);
    lv_anim_init(&s.pop_anim);
    lv_anim_set_var(&s.pop_anim, s.pop_circle);
    lv_anim_set_exec_cb(&s.pop_anim, pop_anim_exec);
    lv_anim_set_values(&s.pop_anim, 0, maxSize);
    lv_anim_set_time(&s.pop_anim, FEEDBACK_POP_MS);
    lv_anim_set_path_cb(&s.pop_anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&s.pop_anim, pop_anim_ready);
    lv_anim_start(&s.pop_anim);

    // Fallback timer in case anim doesn't fire ready_cb
    if (s.t_pop) { lv_timer_del(s.t_pop); s.t_pop = nullptr; }
    s.t_pop = lv_timer_create([](lv_timer_t *) {
        if (s.st == ST_POP) pop_anim_ready(nullptr);
    }, FEEDBACK_POP_MS + 100, nullptr);
    lv_timer_set_repeat_count(s.t_pop, 1);
}

// -- QR (QR code on white tile) ------------------------------------------------
static void qr_timeout_cb(lv_timer_t *) {
    if (s.st == ST_QR) goto_logo_with_cooldown();
}

static void qr_tap_cb(lv_event_t *) {
    if (s.st == ST_QR) goto_logo_with_cooldown();
}

static void goto_logo_with_cooldown(void) {
    if (s.st == ST_COOLDOWN || s.st == ST_LOGO) return;
    // Enable cooldown: absorb taps for cooldownMs
    s.cooldownActive = true;
    if (s.t_cooldown) { lv_timer_del(s.t_cooldown); s.t_cooldown = nullptr; }
    s.t_cooldown = lv_timer_create([](lv_timer_t *) {
        s.cooldownActive = false;
    }, (uint32_t)s.cooldownMs, nullptr);
    lv_timer_set_repeat_count(s.t_cooldown, 1);
    goto_logo();
}

static void rebuild_qr(void) {
    if (!s.v_qr || !s.qr_widget) return;
    const char *url = s.urlReview[0] ? s.urlReview : FEEDBACK_URL_REVIEW;
    lv_qrcode_update(s.qr_widget, url, strlen(url));
}

static void goto_qr(void) {
    s.st = ST_QR;
    if (s.v_logo)   lv_obj_add_flag(s.v_logo, LV_OBJ_FLAG_HIDDEN);
    if (s.v_choice) lv_obj_add_flag(s.v_choice, LV_OBJ_FLAG_HIDDEN);
    if (s.v_pop)    lv_obj_add_flag(s.v_pop, LV_OBJ_FLAG_HIDDEN);
    if (s.v_qr)     lv_obj_clear_flag(s.v_qr, LV_OBJ_FLAG_HIDDEN);
    // Show HUD on QR
    extern void ui_set_hud_visible(bool);
    ui_set_hud_visible(true);

    // 12s auto-return
    if (s.t_qr) { lv_timer_del(s.t_qr); s.t_qr = nullptr; }
    s.t_qr = lv_timer_create(qr_timeout_cb, FEEDBACK_QR_MS, nullptr);
    lv_timer_set_repeat_count(s.t_qr, 1);
}

// =============================================================================
// Admin overlay
// =============================================================================
static void admin_close_cb(lv_event_t *) { feedback_view::setAdminVisible(false); }
static void admin_auto_close_cb(lv_timer_t *) { feedback_view::setAdminVisible(false); }

static void logo_longpress_cb(lv_event_t *e) {
    if (s.st != ST_LOGO) return;
    if (e && lv_event_get_target(e) != s.v_logo) return;
    FB_LOG("[fb] long-press 3s - admin overlay\n");
    feedback_view::setAdminVisible(true);
}

// =============================================================================
// theme API (kept so the web config handlers in main.cpp still compile)
// =============================================================================
namespace feedback_view {
static int     fb_theme   = FEEDBACK_THEME_PHOSPHOR;
static void  (*fb_themeCb)(int) = nullptr;

void setTheme(int t) {
    fb_theme = ((t % FEEDBACK_THEME_COUNT) + FEEDBACK_THEME_COUNT) % FEEDBACK_THEME_COUNT;
    if (fb_themeCb) fb_themeCb(fb_theme);
}
int  theme() { return fb_theme; }
void setThemeChangedCb(void (*cb)(int)) { fb_themeCb = cb; }

// =============================================================================
// Logo loading (SPIFFS on device, local file on sim)
// =============================================================================

#if defined(ESP_PLATFORM)
#  include <SPIFFS.h>
#endif

// Try to load a PNG logo from storage. On success, replaces the LOGO placeholder
// with the image. On failure, shows the placeholder text.
void loadLogo(void) {
    if (!s.v_logo) return;

    uint8_t *png_data = nullptr;
    size_t   png_size = 0;

#if defined(ESP_PLATFORM)
    if (SPIFFS.exists("/logo.png")) {
        File f = SPIFFS.open("/logo.png", "r");
        if (f) {
            png_size = (size_t)f.size();
            if (png_size > 0 && png_size <= 200 * 1024) {
                png_data = (uint8_t *)heap_caps_malloc(png_size, MALLOC_CAP_SPIRAM);
                if (png_data) {
                    if (f.read(png_data, png_size) != (int)png_size) {
                        free(png_data);
                        png_data = nullptr;
                    }
                }
            }
            f.close();
        }
    }
#else
    // Simulator: try to load ./logo.png
    FILE *f = fopen("./logo.png", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        if (sz > 0 && sz <= 200 * 1024) {
            png_data = (uint8_t *)malloc((size_t)sz);
            if (png_data) {
                if (fread(png_data, 1, (size_t)sz, f) != (size_t)sz) {
                    free(png_data);
                    png_data = nullptr;
                } else {
                    png_size = (size_t)sz;
                }
            }
        }
        fclose(f);
    }
#endif

    if (!png_data || png_size == 0) {
        // No logo loaded — show placeholder text
        if (s.logo_img) {
            lv_obj_del(s.logo_img);
            s.logo_img = nullptr;
        }
        if (s.logo_placeholder) {
            lv_obj_clear_flag(s.logo_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Create LVGL image descriptor from raw PNG data
    // LVGL's PNG decoder detects the PNG magic bytes and decodes automatically
    static lv_img_dsc_t img_dsc;
    img_dsc.data = png_data;
    img_dsc.data_size = png_size;
    img_dsc.header.cf = LV_IMG_CF_RAW;
    img_dsc.header.w = 0;   // decoded dimensions will be filled by LVGL
    img_dsc.header.h = 0;

    if (!s.logo_img) {
        s.logo_img = lv_img_create(s.v_logo);
        lv_obj_clear_flag(s.logo_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_img_set_src(s.logo_img, &img_dsc);
    lv_obj_align(s.logo_img, LV_ALIGN_CENTER, s.driftOffX, s.driftOffY);
    lv_obj_clear_flag(s.logo_img, LV_OBJ_FLAG_HIDDEN);

    // Hide placeholder text when logo is shown
    if (s.logo_placeholder) {
        lv_obj_add_flag(s.logo_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

// Remove the logo from storage and reset to placeholder
void removeLogo(void) {
    if (s.logo_img) {
        lv_obj_del(s.logo_img);
        s.logo_img = nullptr;
    }
    if (s.logo_placeholder) {
        lv_obj_clear_flag(s.logo_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
#if defined(ESP_PLATFORM)
    if (SPIFFS.exists("/logo.png")) {
        SPIFFS.remove("/logo.png");
    }
#endif
}

// =============================================================================
// init — build all surfaces once
// =============================================================================

void init(void *lv_parent) {
    if (s.root) return;  // idempotent

    lv_obj_t *parent = (lv_obj_t *)lv_parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Circular-clipped root
    s.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s.root);
    lv_obj_set_size(s.root, SCREEN_W, SCREEN_H);
    lv_obj_center(s.root);
    lv_obj_set_style_radius(s.root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s.root, true, 0);
    lv_obj_set_style_bg_color(s.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);

    // ==================== LOGO screen ====================
    s.v_logo = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_logo);
    lv_obj_set_size(s.v_logo, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_logo);
    lv_obj_set_style_bg_color(s.v_logo, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.v_logo, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.v_logo, LV_OBJ_FLAG_SCROLLABLE);
    // Make clickable for touch-to-wake
    lv_obj_add_flag(s.v_logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.v_logo, logo_tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s.v_logo, logo_longpress_cb, LV_EVENT_LONG_PRESSED, nullptr);

    // Logo image placeholder — will be replaced by logo loading later
    // For now, show large centered question text as placeholder
    s.logo_placeholder = lv_label_create(s.v_logo);
    lv_label_set_text(s.logo_placeholder, FEEDBACK_QUESTION);
    lv_obj_set_style_text_font(s.logo_placeholder, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s.logo_placeholder, COL_DIM, 0);
    lv_obj_set_style_text_align(s.logo_placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s.logo_placeholder, 360);
    lv_obj_align(s.logo_placeholder, LV_ALIGN_CENTER, 0, 0);

    // Drift timer
    s.t_drift = lv_timer_create(logo_drift_cb, FEEDBACK_LOGO_DRIFT_MS, nullptr);
    lv_timer_set_repeat_count(s.t_drift, -1);  // infinite

    // ==================== CHOICE screen (green/red split) ====================
    s.v_choice = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_choice);
    lv_obj_set_size(s.v_choice, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_choice);
    lv_obj_clear_flag(s.v_choice, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.v_choice, LV_OBJ_FLAG_HIDDEN);

    // Left half — green
    s.choice_left = lv_obj_create(s.v_choice);
    lv_obj_remove_style_all(s.choice_left);
    lv_obj_set_size(s.choice_left, SCREEN_W / 2, SCREEN_H);
    lv_obj_align(s.choice_left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s.choice_left, COL_GREEN, 0);
    lv_obj_set_style_bg_opa(s.choice_left, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.choice_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.choice_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(s.choice_left, (void *)(intptr_t)FEEDBACK_GOOD);
    lv_obj_add_event_cb(s.choice_left, choice_tap_cb, LV_EVENT_CLICKED, nullptr);

    // Happy smiley on green
    make_smiley(s.choice_left, FEEDBACK_GOOD, FEEDBACK_HITBOX_MIN, FEEDBACK_SMILEY_DIA);
    lv_obj_center(lv_obj_get_child(s.choice_left, 0));

    // Right half — red
    s.choice_right = lv_obj_create(s.v_choice);
    lv_obj_remove_style_all(s.choice_right);
    lv_obj_set_size(s.choice_right, SCREEN_W / 2, SCREEN_H);
    lv_obj_align(s.choice_right, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s.choice_right, COL_RED, 0);
    lv_obj_set_style_bg_opa(s.choice_right, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.choice_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.choice_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(s.choice_right, (void *)(intptr_t)FEEDBACK_BAD);
    lv_obj_add_event_cb(s.choice_right, choice_tap_cb, LV_EVENT_CLICKED, nullptr);

    // Unhappy smiley on red
    make_smiley(s.choice_right, FEEDBACK_BAD, FEEDBACK_HITBOX_MIN, FEEDBACK_SMILEY_DIA);
    lv_obj_center(lv_obj_get_child(s.choice_right, 0));

    // ==================== POP screen (transparent bg, expanding circle) ====================
    s.v_pop = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_pop);
    lv_obj_set_size(s.v_pop, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_pop);
    lv_obj_set_style_bg_opa(s.v_pop, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s.v_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.v_pop, LV_OBJ_FLAG_HIDDEN);

    // ==================== QR screen ====================
    s.v_qr = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_qr);
    lv_obj_set_size(s.v_qr, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_qr);
    lv_obj_set_style_bg_color(s.v_qr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.v_qr, LV_OPA_COVER, 0);
    lv_obj_add_flag(s.v_qr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.v_qr, qr_tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s.v_qr, LV_OBJ_FLAG_HIDDEN);

    // White rounded tile for QR code (~300x300 fits the round 466 display)
    lv_obj_t *qr_tile = lv_obj_create(s.v_qr);
    lv_obj_remove_style_all(qr_tile);
    lv_obj_set_size(qr_tile, 300, 300);
    lv_obj_center(qr_tile);
    lv_obj_set_style_bg_color(qr_tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qr_tile, 8, 0);
    lv_obj_clear_flag(qr_tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // QR widget inside the tile — QR code at max size that fits inside 300px with quiet zone
    s.qr_widget = lv_qrcode_create(qr_tile, 270, lv_color_black(), lv_color_white());
    lv_qrcode_update(s.qr_widget, FEEDBACK_URL_REVIEW, strlen(FEEDBACK_URL_REVIEW));
    lv_obj_align(s.qr_widget, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s.qr_widget, LV_OBJ_FLAG_CLICKABLE);

    // ==================== Admin overlay ====================
    s.v_admin = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_admin);
    lv_obj_set_size(s.v_admin, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_admin);
    lv_obj_set_style_bg_color(s.v_admin, COL_BG, 0);
    lv_obj_set_style_bg_opa(s.v_admin, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s.v_admin, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s.v_admin, true, 0);
    lv_obj_clear_flag(s.v_admin, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.v_admin, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *aTitle = lv_label_create(s.v_admin);
    lv_label_set_text(aTitle, "FEEDBACK KIOSK");
    lv_obj_set_style_text_font(aTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(aTitle, COL_GREEN, 0);
    lv_obj_align(aTitle, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *sec = lv_label_create(s.v_admin);
    lv_label_set_text(sec, "VANDAAG");
    lv_obj_set_style_text_font(sec, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sec, COL_SOFT, 0);
    lv_obj_set_style_text_opa(sec, 180, 0);
    lv_obj_align(sec, LV_ALIGN_TOP_MID, 0, 120);

    s.lbl_today = lv_label_create(s.v_admin);
    lv_label_set_text(s.lbl_today, "GOED 0   ONTEVREDEN 0");
    lv_obj_set_style_text_font(s.lbl_today, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s.lbl_today, COL_INK, 0);
    lv_obj_align(s.lbl_today, LV_ALIGN_TOP_MID, 0, 148);

    lv_obj_t *sec2 = lv_label_create(s.v_admin);
    lv_label_set_text(sec2, "CONFIGURATIE");
    lv_obj_set_style_text_font(sec2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sec2, COL_SOFT, 0);
    lv_obj_set_style_text_opa(sec2, 180, 0);
    lv_obj_align(sec2, LV_ALIGN_TOP_MID, 0, 196);

    s.lbl_ip = lv_label_create(s.v_admin);
    lv_label_set_text(s.lbl_ip, "IP: -");
    lv_obj_set_style_text_font(s.lbl_ip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s.lbl_ip, COL_INK, 0);
    lv_obj_align(s.lbl_ip, LV_ALIGN_TOP_MID, 0, 222);

    s.lbl_url = lv_label_create(s.v_admin);
    lv_label_set_text(s.lbl_url, "http://feedback.local/");
    lv_obj_set_style_text_font(s.lbl_url, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s.lbl_url, COL_GREEN, 0);
    lv_obj_align(s.lbl_url, LV_ALIGN_TOP_MID, 0, 244);

    lv_obj_t *close = lv_btn_create(s.v_admin);
    lv_obj_set_size(close, 200, 50);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_style_radius(close, 12, 0);
    lv_obj_set_style_bg_color(close, COL_GREEN, 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_t *closeLbl = lv_label_create(close);
    lv_label_set_text(closeLbl, "Sluiten");
    lv_obj_set_style_text_color(closeLbl, COL_BG, 0);
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(closeLbl);
    lv_obj_add_event_cb(close, admin_close_cb, LV_EVENT_CLICKED, nullptr);

    // Start in LOGO state
    goto_logo();
    // Try to load logo from storage
    loadLogo();
    FB_LOG("[fb] state machine ready (LOGO -> CHOICE -> POP -> QR -> LOGO)\n");
}

// =============================================================================
// Live operator setters
// =============================================================================
void setMode(int m) { s.mode = (m == 1) ? 1 : 0; }

void setCooldownMs(int ms) {
    s.cooldownMs = (ms < 1000) ? 1000 : (ms > 30000 ? 30000 : ms);
}

void setIdleBrightness(int pct) {
    s.idleBrightness = (pct < 0) ? 0 : (pct > 100 ? 100 : pct);
}

void setQuestion(const char *text) {
    if (!text || !s.logo_placeholder) return;
    lv_label_set_text(s.logo_placeholder, text);
    if (s.logo_img) {
        // If a logo image exists, keep it; otherwise placeholder shows text
        lv_obj_add_flag(s.logo_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s.logo_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

void setUrlReview(const char *url) {
    if (!url || !s.qr_widget) return;
    strncpy(s.urlReview, url, sizeof(s.urlReview) - 1);
    s.urlReview[sizeof(s.urlReview) - 1] = 0;
    lv_qrcode_update(s.qr_widget, s.urlReview, strlen(s.urlReview));
    lv_obj_invalidate(s.qr_widget);
}

void setUrlInternal(const char *) {
    // Kept for API compat — unused in 2-answer flow (only one QR shown)
}

// Admin overlay
void setAdminVisible(bool on) {
    if (!s.v_admin) return;
    if (on) {
        lv_obj_clear_flag(s.v_admin, LV_OBJ_FLAG_HIDDEN);
        if (s.t_admin) {
            lv_timer_set_repeat_count(s.t_admin, 1);
            lv_timer_reset(s.t_admin);
        } else {
            s.t_admin = lv_timer_create(admin_auto_close_cb, FEEDBACK_ADMIN_PING_MS, nullptr);
            lv_timer_set_repeat_count(s.t_admin, 1);
        }
        FB_LOG("[fb] admin overlay open\n");
    } else {
        lv_obj_add_flag(s.v_admin, LV_OBJ_FLAG_HIDDEN);
        if (s.t_admin) lv_timer_pause(s.t_admin);
        FB_LOG("[fb] admin overlay close\n");
    }
}

void setAdminIp(const char *ip) {
    if (!ip || !s.lbl_ip || !s.lbl_url) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "IP: %s", ip);
    lv_label_set_text(s.lbl_ip, buf);
    snprintf(buf, sizeof(buf), "http://%s/", ip);
    lv_label_set_text(s.lbl_url, buf);
}

void setAdminToday(uint32_t good, uint32_t bad) {
    if (!s.lbl_today) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "GOED %lu   ONTEVREDEN %lu",
             (unsigned long)good, (unsigned long)bad);
    lv_label_set_text(s.lbl_today, buf);
}

} // namespace feedback_view

