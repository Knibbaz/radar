// Customer-feedback kiosk view: smiley rating + QR follow-up.
//
// State machine: IDLE -> THANKS -> QR -> IDLE (with a COOLDOWN shielding
// taps from repeated use). Built on top of LVGL v8.
//
// `init()` builds all four state surfaces once as children of a circular
// clipped root container; transitions just show/hide them. Each transition
// uses LVGL timers (and the audio module on the device) for timing --
// there is no busy loop.

#include "feedback_view.h"
#include "config.h"

// Audio path. The native SDL simulator doesn't link audio.cpp, so guard
// the include + the call. On real hardware, AUDIO_NEW is a soft single
// beep already exposed by the alert-ping path in main.cpp.
#if defined(ESP_PLATFORM)
#  include "audio.h"
#endif

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// Serial.printf on device, stderr on the sim (where Arduino.h/Serial don't
// exist). Matches the gating lv_conf.h uses for the Arduino tick fallback.
#if defined(ESP_PLATFORM)
#  include <Arduino.h>
#  define FB_LOG(...) Serial.printf(__VA_ARGS__)
#else
#  define FB_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

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

// radar-era knobs -- the radar view used to react to these; now they're
// no-ops so the persisted settings don't break the build.
void setRangeLabelVisible(bool) {}
void setSweepEnabled(bool)     {}
bool sweepEnabled()            { return false; }
void setAirportsEnabled(bool)  {}
bool airportsEnabled()         { return false; }
void setTrailLength(int)       {}
void setMaxOnScreen(int)       {}

} // namespace feedback_view

// =============================================================================
// state machine
// =============================================================================
enum FbState { ST_IDLE, ST_THANKS, ST_QR_GOOD, ST_QR_BAD, ST_COOLDOWN };

namespace {
struct S {
    FbState        st         = ST_IDLE;
    FeedbackAnswer lastAnswer = FEEDBACK_GOOD;

    // surfaces (all children of a circular-clipped root container)
    lv_obj_t      *root      = nullptr;
    lv_obj_t      *v_idle    = nullptr;
    lv_obj_t      *v_thanks  = nullptr;
    lv_obj_t      *v_qr_good = nullptr;
    lv_obj_t      *v_qr_bad  = nullptr;
    lv_obj_t      *arc_cd    = nullptr;

    // live-tunable (pushed by feedback_log::applySettings)
    int            mode       = 0;          // 0=REVIEW, 1=DASHBOARD
    int            cooldownMs = FEEDBACK_COOLDOWN_MS;

    // question label + QR widget handles for live updates
    lv_obj_t      *lbl_question = nullptr;
    lv_obj_t      *qr_review    = nullptr;   // FEEDBACK_URL_REVIEW  widget in v_qr_good
    lv_obj_t      *qr_internal  = nullptr;   // FEEDBACK_URL_INTERNAL in v_qr_bad
    lv_obj_t      *qr_review2   = nullptr;   // secondary "Or leave a public review" widget

    lv_timer_t    *t_thanks  = nullptr;
    lv_timer_t    *t_qr      = nullptr;
    lv_anim_t      a_cd;
} s;
} // anonymous namespace


// ---- cooldown progress arc ----------------------------------------------------
static void arc_cd_exec(void *obj, int32_t v) {
    lv_arc_set_angles((lv_obj_t *)obj, 0, (uint16_t)v);
}
static void arc_cd_ready(lv_anim_t *) {
    if (s.arc_cd) lv_obj_add_flag(s.arc_cd, LV_OBJ_FLAG_HIDDEN);
    s.st = ST_IDLE;                                // cooldown done -> back to IDLE
}
static void start_cd_anim(void) {
    lv_anim_init(&s.a_cd);
    lv_anim_set_var(&s.a_cd, s.arc_cd);
    lv_anim_set_exec_cb(&s.a_cd, arc_cd_exec);
    lv_anim_set_values(&s.a_cd, 0, 360);
    lv_anim_set_time(&s.a_cd, s.cooldownMs);     // live-tunable
    lv_anim_set_path_cb(&s.a_cd, lv_anim_path_linear);
    lv_anim_set_ready_cb(&s.a_cd, arc_cd_ready);
    lv_anim_start(&s.a_cd);
}

// QR -> IDLE + COOLDOWN. Idempotent: a delayed t_qr firing during the
// cooldown is a no-op.
static void goto_idle_with_cooldown(void) {
    if (s.st == ST_COOLDOWN) return;
    s.st = ST_COOLDOWN;
    lv_obj_add_flag(s.v_qr_good, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s.v_qr_bad,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s.v_idle,  LV_OBJ_FLAG_HIDDEN);
    if (s.arc_cd) {
        lv_obj_clear_flag(s.arc_cd, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_angles(s.arc_cd, 0, 0);
        start_cd_anim();
    }
}

static void t_qr_cb(lv_timer_t *) { goto_idle_with_cooldown(); }

static void t_thanks_cb(lv_timer_t *) {
    lv_obj_add_flag(s.v_thanks, LV_OBJ_FLAG_HIDDEN);

    // Dashboard mode: skip QR entirely. After the THANK-YOU pulse we go
    // straight back to IDLE through the normal cooldown arc.
    if (s.mode == 1 /* DASHBOARD */) {
        goto_idle_with_cooldown();
        return;
    }

    s.st = (s.lastAnswer == FEEDBACK_GOOD) ? ST_QR_GOOD : ST_QR_BAD;
    if (s.st == ST_QR_GOOD) lv_obj_clear_flag(s.v_qr_good, LV_OBJ_FLAG_HIDDEN);
    else                    lv_obj_clear_flag(s.v_qr_bad,  LV_OBJ_FLAG_HIDDEN);
    // arm the QR -> IDLE one-shot
    s.t_qr = lv_timer_create(t_qr_cb, FEEDBACK_QR_MS, nullptr);
    lv_timer_set_repeat_count(s.t_qr, 1);
}

// IDLE -> THANKS
static void goto_thanks(FeedbackAnswer a) {
    if (s.st != ST_IDLE) return;                   // defensive
    s.lastAnswer = a;
    {
        const char *lbl = (a == FEEDBACK_GOOD) ? "GOOD" :
                          (a == FEEDBACK_NEUTRAL) ? "NEUTRAL" : "BAD";
        FB_LOG("[fb] %s tap recorded (event log deferred)\n", lbl);
    }
    s.st = ST_THANKS;
    lv_obj_add_flag(s.v_idle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s.v_thanks, LV_OBJ_FLAG_HIDDEN);
#if defined(ESP_PLATFORM)
    audio_play(AUDIO_NEW);                         // soft ping (no-op if no audio hw)
#endif
    s.t_thanks = lv_timer_create(t_thanks_cb, FEEDBACK_THANKS_MS, nullptr);
    lv_timer_set_repeat_count(s.t_thanks, 1);
}

// ---- event handlers ---------------------------------------------------------
static void smiley_cb(lv_event_t *e) {
    lv_obj_t *t = lv_event_get_target(e);
    FeedbackAnswer a = (FeedbackAnswer)(intptr_t)lv_obj_get_user_data(t);
    if (s.st != ST_IDLE) return;                   // COOLDOWN: ignore repeated taps
    goto_thanks(a);
}
static void qr_tap_cb(lv_event_t *) {
    if (s.st != ST_QR_GOOD && s.st != ST_QR_BAD) return;
    goto_idle_with_cooldown();                     // tap = skip the 12s wait
}
static void longpress_cb(lv_event_t *) {
    FB_LOG("[fb] long-press 3s - admin menu reserved\n");
}

// =============================================================================
// surface builders
// =============================================================================

// Smiley face: a round, colored hit-box with eyes + (arc|flat) mouth inside.
static lv_obj_t *make_smiley(lv_obj_t *parent, FeedbackAnswer ans,
                              lv_color_t fill, lv_coord_t hit, lv_coord_t dia) {
    // hit box + colour (the round visible button)
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, hit, hit);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, fill, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(btn, (void *)(intptr_t)ans);
    lv_obj_add_event_cb(btn, smiley_cb,    LV_EVENT_CLICKED,      nullptr);
    lv_obj_add_event_cb(btn, longpress_cb, LV_EVENT_LONG_PRESSED, nullptr);

    // eyes (two small dark dots, symmetric)
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

    // mouth: flat line for NEUTRAL, an arc segment for GOOD/BAD
    if (ans == FEEDBACK_NEUTRAL) {
        lv_obj_t *m = lv_obj_create(btn);
        lv_obj_remove_style_all(m);
        lv_obj_set_size(m, dia / 2, dia / 22 + 2);
        lv_obj_set_style_bg_color(m, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_align(m, LV_ALIGN_CENTER, 0, dia / 8);
        lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    } else {
        // 0=east, 90=south, 180=west, 270=north. Indicator drawn CW.
        // Smile (U):  30..150  -> bottom rim of arc circle -> U-shape.
        // Frown (^): 210..330  -> top rim    of arc circle -> inverted U.
        lv_obj_t *m = lv_arc_create(btn);
        lv_obj_set_size(m, dia * 3 / 4, dia * 3 / 4);
        lv_obj_align(m, LV_ALIGN_CENTER, 0, dia / 8);
        lv_obj_set_style_arc_opa(m, LV_OPA_TRANSP, LV_PART_MAIN);     // hide track ring
        lv_obj_set_style_arc_width(m, dia / 14, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(m, lv_color_black(), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(m, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_arc_set_angles(m, ans == FEEDBACK_GOOD ? 30 : 210,
                              ans == FEEDBACK_GOOD ? 150 : 330);
        lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    return btn;
}

// QR card: a white card hosting a LVGL QR widget of the given pixel size.
static lv_obj_t *make_qr_card(lv_obj_t *parent, lv_coord_t size, const char *url) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, size + 16, size + 16);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *qr = lv_qrcode_create(card, size, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(qr, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

// =============================================================================
// init
// =============================================================================
namespace feedback_view {

void init(void *lv_parent) {
    if (s.root) return;                            // idempotent

    lv_obj_t *parent = (lv_obj_t *)lv_parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Circular clipped root: every state surface lives inside this, so even
    // long QR cards / the cooldown arc stay inside the round AMOLED bezel.
    s.root = lv_obj_create(parent);
    lv_obj_remove_style_all(s.root);
    lv_obj_set_size(s.root, SCREEN_W, SCREEN_H);
    lv_obj_center(s.root);
    lv_obj_set_style_radius(s.root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s.root, true, 0);
    lv_obj_set_style_bg_color(s.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);

    // -------------------- IDLE: question + 3 smileys ------------------------
    s.v_idle = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_idle);
    lv_obj_set_size(s.v_idle, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_idle);
    lv_obj_clear_flag(s.v_idle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // long-press on IDLE's empty area (the smileys have their own handler)
    lv_obj_add_event_cb(s.v_idle, longpress_cb, LV_EVENT_LONG_PRESSED, nullptr);

    lv_obj_t *q = lv_label_create(s.v_idle);
    lv_label_set_text(q, FEEDBACK_QUESTION);
    lv_obj_set_style_text_font(q, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(q, lv_color_hex(0xEAFFF3), 0);
    lv_obj_set_width(q, 360);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, 110);
    s.lbl_question = q;                         // live-updatable from feedback_log::applySettings()

    // 3 smileys, evenly spaced horizontally
    static const lv_color_t cols[3] = {
        lv_color_hex(0x1DFF86),    // GOOD    - phosphor green
        lv_color_hex(0xFFB23C),    // NEUTRAL - amber
        lv_color_hex(0xFF5A3C)     // BAD     - red
    };
    static const FeedbackAnswer ans[3] = {
        FEEDBACK_GOOD, FEEDBACK_NEUTRAL, FEEDBACK_BAD
    };
    for (int i = 0; i < 3; ++i) {
        const lv_coord_t dx = (i - 1) * 130;      // -130, 0, +130 from SCREEN_CX
        lv_obj_t *btn = make_smiley(s.v_idle, ans[i], cols[i],
                                    FEEDBACK_HITBOX_MIN, FEEDBACK_SMILEY_DIA);
        lv_obj_align(btn, LV_ALIGN_CENTER, dx, 0);
    }

    // -------------------- THANKS: full-screen green panel -------------------
    s.v_thanks = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_thanks);
    lv_obj_set_size(s.v_thanks, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_thanks);
    lv_obj_set_style_bg_color(s.v_thanks, lv_color_hex(0x1DFF86), 0);
    lv_obj_set_style_bg_opa(s.v_thanks, LV_OPA_COVER, 0);
    lv_obj_add_flag(s.v_thanks, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *ok = lv_label_create(s.v_thanks);
        lv_label_set_text(ok, LV_SYMBOL_OK);                   // built-in check icon
        lv_obj_set_style_text_font(ok, &lv_font_montserrat_28, 0); // largest bundled font
        lv_obj_set_style_text_color(ok, lv_color_white(), 0);
        lv_obj_align(ok, LV_ALIGN_CENTER, 0, -30);

        lv_obj_t *ty = lv_label_create(s.v_thanks);
        lv_label_set_text(ty, "Bedankt voor uw feedback!");
        lv_obj_set_style_text_font(ty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ty, lv_color_white(), 0);
        lv_obj_align(ty, LV_ALIGN_CENTER, 0, 60);
    }

    // -------------------- QR (GOOD): single large review QR -----------------
    s.v_qr_good = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_qr_good);
    lv_obj_set_size(s.v_qr_good, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_qr_good);
    lv_obj_set_style_bg_color(s.v_qr_good, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.v_qr_good, LV_OPA_COVER, 0);
    lv_obj_add_flag(s.v_qr_good, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.v_qr_good, qr_tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s.v_qr_good, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *lbl = lv_label_create(s.v_qr_good);
        lv_label_set_text(lbl, "Blij met ons? Laat een review achter!");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAFFF3), 0);
        lv_obj_set_width(lbl, 360);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 110);

        lv_obj_t *card = make_qr_card(s.v_qr_good, 140, FEEDBACK_URL_REVIEW);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
        s.qr_review = lv_obj_get_child(card, 0);          // the QR widget is the only child of the card

        lv_obj_t *url = lv_label_create(s.v_qr_good);
        lv_label_set_text(url, FEEDBACK_URL_REVIEW);
        lv_obj_set_style_text_font(url, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(url, lv_color_hex(0x9AFFC8), 0);
        lv_obj_align(url, LV_ALIGN_CENTER, 0, 110);

        lv_obj_t *hint = lv_label_create(s.v_qr_good);
        lv_label_set_text(hint, "Tik om terug te gaan");
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x9AFFC8), 0);
        lv_obj_set_style_text_opa(hint, 140, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    }

    // ----- QR (NEUTRAL/BAD): internal form (large, centered) + small review -
    s.v_qr_bad = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.v_qr_bad);
    lv_obj_set_size(s.v_qr_bad, SCREEN_W, SCREEN_H);
    lv_obj_center(s.v_qr_bad);
    lv_obj_set_style_bg_color(s.v_qr_bad, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.v_qr_bad, LV_OPA_COVER, 0);
    lv_obj_add_flag(s.v_qr_bad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.v_qr_bad, qr_tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s.v_qr_bad, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *lbl = lv_label_create(s.v_qr_bad);
        lv_label_set_text(lbl, "Vertel ons wat beter kan");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xEAFFF3), 0);
        lv_obj_set_width(lbl, 360);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 92);

        lv_obj_t *card1 = make_qr_card(s.v_qr_bad, 110, FEEDBACK_URL_INTERNAL);
        lv_obj_align(card1, LV_ALIGN_CENTER, 0, -50);
        s.qr_internal = lv_obj_get_child(card1, 0);

        lv_obj_t *url1 = lv_label_create(s.v_qr_bad);
        lv_label_set_text(url1, FEEDBACK_URL_INTERNAL);
        lv_obj_set_style_text_font(url1, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(url1, lv_color_hex(0x9AFFC8), 0);
        lv_obj_align(url1, LV_ALIGN_CENTER, 0, 35);

        lv_obj_t *orL = lv_label_create(s.v_qr_bad);
        lv_label_set_text(orL, "Of laat een publieke review achter:");
        lv_obj_set_style_text_font(orL, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(orL, lv_color_hex(0xEAFFF3), 0);
        lv_obj_set_style_text_opa(orL, 200, 0);
        lv_obj_align(orL, LV_ALIGN_CENTER, 0, 65);

        lv_obj_t *card2 = make_qr_card(s.v_qr_bad, 60, FEEDBACK_URL_REVIEW);
        lv_obj_align(card2, LV_ALIGN_CENTER, 0, 110);
        s.qr_review2 = lv_obj_get_child(card2, 0);

        lv_obj_t *url2 = lv_label_create(s.v_qr_bad);
        lv_label_set_text(url2, FEEDBACK_URL_REVIEW);
        lv_obj_set_style_text_font(url2, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(url2, lv_color_hex(0x9AFFC8), 0);
        lv_obj_align(url2, LV_ALIGN_CENTER, 0, 155);

        lv_obj_t *hint = lv_label_create(s.v_qr_bad);
        lv_label_set_text(hint, "Tik om terug te gaan");
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x9AFFC8), 0);
        lv_obj_set_style_text_opa(hint, 140, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    }

    // --------------- Cooldown arc (full-screen ring; animated) -------------
    s.arc_cd = lv_arc_create(s.root);
    lv_obj_set_size(s.arc_cd, 432, 432);
    lv_obj_align(s.arc_cd, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_color(s.arc_cd, lv_color_hex(0x1DFF86), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s.arc_cd, 50,  LV_PART_MAIN);          // dim track
    lv_obj_set_style_arc_width(s.arc_cd, 4,  LV_PART_MAIN);
    lv_obj_set_style_arc_color(s.arc_cd, lv_color_hex(0x1DFF86), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s.arc_cd, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s.arc_cd, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s.arc_cd, LV_OPA_TRANSP, LV_PART_KNOB); // hide knob dot
    lv_arc_set_angles(s.arc_cd, 0, 0);
    lv_obj_clear_flag(s.arc_cd, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.arc_cd, LV_OBJ_FLAG_HIDDEN);

    // 3 s long-press -> "admin menu" (reserved). LVGL v8.4 didn't expose the
    // setter we wanted on this build, so we just iterate the input devices to
    // prove the wiring (and keep the no-op for future sym-version probing).
    for (lv_indev_t *ind = lv_indev_get_next(nullptr); ind; ind = lv_indev_get_next(ind)) {
        (void)ind;   // admin-menu handler will own the long-press on both envs
    }

    FB_LOG("[fb] state machine ready (IDLE / THANKS / QR / COOLDOWN)\n");
}

void update(void) {}

// -----------------------------------------------------------------------------
// Live operator setters (feedback_log::applySettings + web handlers)
// -----------------------------------------------------------------------------
void setMode(int mode) { s.mode = (mode == 1) ? 1 : 0; }

void setCooldownMs(int ms) {
    s.cooldownMs = (ms < 2000) ? 2000 : (ms > 30000 ? 30000 : ms);
}

void setQuestion(const char *text) {
    if (!text || !s.lbl_question) return;
    lv_label_set_text(s.lbl_question, text);
}

void setUrlReview(const char *url) {
    if (!url || !s.qr_review) return;
    lv_qrcode_update(s.qr_review, url, strlen(url));
    lv_obj_invalidate(s.qr_review);
    if (s.qr_review2) {                                       // secondary "public review" widget
        lv_qrcode_update(s.qr_review2, url, strlen(url));
        lv_obj_invalidate(s.qr_review2);
    }
}

void setUrlInternal(const char *url) {
    if (!url || !s.qr_internal) return;
    lv_qrcode_update(s.qr_internal, url, strlen(url));
    lv_obj_invalidate(s.qr_internal);
}

// Smooth out a string into a label, replacing unsupported chars with '?' so we
// never hit a missing-glyph code point under the bundled Montserrat font.
static void safe_set_label(lv_obj_t *lbl, const char *s) {
    if (!lbl) return;
    char buf[FEEDBACK_MAX_QUESTION + 1];
    size_t n = 0;
    if (s) while (*s && n < sizeof(buf) - 1) {
        unsigned char c = (unsigned char)*s++;
        buf[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    buf[n] = 0;
    lv_label_set_text(lbl, buf);
}

void setQuestionSafe(const char *text) { safe_set_label(s.lbl_question, text); }

} // namespace feedback_view
