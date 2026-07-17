// M3 UI (terminal edition): top HUD only (WiFi / clock / battery / date).
// The full-screen main canvas is built by feedback_view::init() — the
// tileview, detail card, list/stats and zoom button from the radar build
// are gone. Public API in ui.h is preserved as no-op stubs so callers in
// main.cpp compile unchanged.
#include "ui.h"
#include "feedback_view.h"
#include "config.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define UI_GREEN lv_color_hex(0x1DFF86)
#define UI_INK   lv_color_hex(0xEAFFF3)
#define UI_SOFT  lv_color_hex(0x9AFFC8)
#define UI_DIM   lv_color_hex(0x5F7A6C)
#define UI_PANEL lv_color_hex(0x0C160F)
#define UI_EMERG lv_color_hex(0xFF5A3C)

static lv_obj_t *s_hudWifi = nullptr, *s_hudClock = nullptr,
                *s_hudBatt = nullptr, *s_hudDate = nullptr;
static lv_obj_t *s_hudBars[4] = { nullptr, nullptr, nullptr, nullptr };   // WiFi signal-strength bars
static bool      s_hudVisible = true;

// --------------------------------------------------------------------- HUD
// Bar count from RSSI (dBm): the weaker the signal, the fewer lit bars.
void ui_set_status(bool wifiUp, bool feedOk, int rssi, const char *clock) {
    int level;
    if      (!wifiUp)     level = 0;
    else if (rssi >= -55) level = 4;   // excellent
    else if (rssi >= -67) level = 3;   // good
    else if (rssi >= -75) level = 2;   // ok
    else                  level = 1;   // weak (connected but marginal)
    // colour: red = no WiFi, amber = stale feed (no fresh data), white = healthy.
    // The terminal has no feed, so feedOk is treated as "WiFi is connected".
    const lv_color_t col = !wifiUp ? UI_EMERG : (feedOk ? UI_INK : lv_color_hex(0xFFB23C));
    for (int i = 0; i < 4; ++i) {
        if (!s_hudBars[i]) continue;
        lv_obj_set_style_bg_color(s_hudBars[i], col, 0);
        lv_obj_set_style_bg_opa(s_hudBars[i], (i < level) ? LV_OPA_COVER : 45, 0);
    }
    if (s_hudClock && clock) lv_label_set_text(s_hudClock, clock);
}

void ui_set_battery(int pct, bool charging, bool present) {
    if (!s_hudBatt) return;
    if (!present || pct < 0) { lv_label_set_text(s_hudBatt, ""); return; }   // USB-only -> hide
    const char *sym = pct > 80 ? LV_SYMBOL_BATTERY_FULL :
                      pct > 55 ? LV_SYMBOL_BATTERY_3 :
                      pct > 35 ? LV_SYMBOL_BATTERY_2 :
                      pct > 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%s%d", charging ? LV_SYMBOL_CHARGE : "", sym, pct);
    lv_label_set_text(s_hudBatt, buf);
    lv_obj_set_style_text_color(s_hudBatt, (pct <= 15 && !charging) ? UI_EMERG : UI_INK, 0);
}

void ui_set_date(const char *date) {
    if (s_hudDate && date) lv_label_set_text(s_hudDate, date);
}

void ui_set_netinfo(const char *) { /* Stats tile is gone; main.cpp's loop still calls this. */ }

void ui_set_hud_visible(bool visible) {
    s_hudVisible = visible;
    const lv_opa_t opa = visible ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (s_hudWifi)  lv_obj_set_style_opa(s_hudWifi, opa, 0);
    if (s_hudClock) lv_obj_set_style_opa(s_hudClock, opa, 0);
    if (s_hudBatt)  lv_obj_set_style_opa(s_hudBatt, opa, 0);
    if (s_hudDate)  lv_obj_set_style_opa(s_hudDate, opa, 0);
}

// The terminal doesn't use GPS; main.cpp's loop still calls this. No-op.
void ui_set_gps(int, int) {
    if (s_hudBars[0]) { /* keeps the symbol referenced so the file isn't "unused variable" */ }
}

// No-op stubs for the radar-era callbacks. Settings round-trip through NVS but
// no longer render anything; these exist so ui.h's API stays intact.
void ui_set_range_cb(void (*)(float))   {}
void ui_set_range_km(float)             {}
void ui_set_units(int)                  {}
void ui_show_view(int)                  {}
void ui_on_data_updated(void)           {}

// ------------------------------------------------------------------- splash
// Neutral "Feedback" boot splash. No radar-era sweep / concentric rings —
// just a centered smiley glyph + brand text, then a soft fade-out. The
// splash is shown on top of the active screen while the feedback view is
// being built, so it must not assume the view exists yet.
static void splash_fade_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }
static void splash_del_cb(lv_anim_t *a) { lv_obj_del((lv_obj_t *)a->var); }

static void splash_dismiss_cb(lv_timer_t *t) {
    lv_obj_t *cont = (lv_obj_t *)t->user_data;
    lv_timer_del(t);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, cont);
    lv_anim_set_exec_cb(&a, splash_fade_cb);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_time(&a, 600);
    lv_anim_set_ready_cb(&a, splash_del_cb);
    lv_anim_start(&a);
}

void ui_splash_show(void) {
    lv_obj_t *cont = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, SCREEN_W, SCREEN_H);
    lv_obj_center(cont);
    lv_obj_set_style_bg_color(cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Soft phosphor-green disc as a brand mark (no radar sweep, no rings).
    lv_obj_t *disc = lv_obj_create(cont);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 180, 180);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, UI_GREEN, 0);
    lv_obj_set_style_bg_opa(disc, 35, 0);                       // dim fill, not a solid block
    lv_obj_set_style_border_color(disc, UI_GREEN, 0);
    lv_obj_set_style_border_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 3, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

    // Smiley glyph (LV_SYMBOL's "smile" is a face; we use OK for a clean tick
    // since the splash is brand-only, not a preview of the rating UI).
    lv_obj_t *mark = lv_label_create(cont);
    lv_label_set_text(mark, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(mark, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(mark, UI_GREEN, 0);
    lv_obj_align(mark, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "FEEDBACK");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, UI_GREEN, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 90);

    lv_obj_t *sub = lv_label_create(cont);
    lv_label_set_text(sub, "Customer feedback terminal");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, UI_SOFT, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 122);

    lv_timer_t *t = lv_timer_create(splash_dismiss_cb, 1800, cont);   // hold, then fade out
    lv_timer_set_repeat_count(t, 1);
}

void ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Full-screen placeholder content (black bg + centered "FEEDBACK KIOSK"
    // label). The actual smiley / rating UI replaces this in a later step.
    feedback_view::init(scr);

    // top status HUD: WiFi (4 bars, RSSI) + clock + battery + date.
    s_hudWifi = lv_obj_create(scr);
    lv_obj_remove_style_all(s_hudWifi);
    lv_obj_set_size(s_hudWifi, 21, 14);
    lv_obj_align(s_hudWifi, LV_ALIGN_TOP_MID, -94, 50);
    lv_obj_clear_flag(s_hudWifi, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 4; ++i) {
        s_hudBars[i] = lv_obj_create(s_hudWifi);
        lv_obj_remove_style_all(s_hudBars[i]);
        lv_obj_set_size(s_hudBars[i], 3, (lv_coord_t)(4 + i * 3));   // 4, 7, 10, 13 px tall
        lv_obj_align(s_hudBars[i], LV_ALIGN_BOTTOM_LEFT, (lv_coord_t)(i * 5), 0);
        lv_obj_set_style_radius(s_hudBars[i], 1, 0);
        lv_obj_set_style_bg_color(s_hudBars[i], UI_INK, 0);
        lv_obj_set_style_bg_opa(s_hudBars[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_hudBars[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    s_hudClock = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hudClock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hudClock, UI_INK, 0);
    lv_label_set_text(s_hudClock, "--:--");
    lv_obj_align(s_hudClock, LV_ALIGN_TOP_MID, 30, 50);

    s_hudBatt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hudBatt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hudBatt, UI_INK, 0);
    lv_label_set_text(s_hudBatt, "");
    lv_obj_align(s_hudBatt, LV_ALIGN_TOP_MID, 92, 50);

    s_hudDate = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hudDate, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hudDate, UI_INK, 0);
    lv_obj_set_style_text_opa(s_hudDate, 140, 0);
    lv_label_set_text(s_hudDate, "");
    lv_obj_align(s_hudDate, LV_ALIGN_TOP_MID, 0, 70);

    ui_splash_show();   // branded boot splash on top (auto-fades)
}
