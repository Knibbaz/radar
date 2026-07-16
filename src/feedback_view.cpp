// Placeholder view for the customer-feedback terminal.
// Currently a single centered label on true black; the actual smiley /
// rating UI will replace it. Theme + the radar-era knobs are no-ops so the
// existing web config handlers and NVS keys round-trip cleanly.
#include "feedback_view.h"
#include <lvgl.h>

static int  s_theme = FEEDBACK_THEME_PHOSPHOR;
static void (*s_themeCb)(int) = nullptr;

namespace feedback_view {

void init(void *lv_parent) {
    lv_obj_t *parent = (lv_obj_t *)lv_parent;
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "FEEDBACK KIOSK");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1DFF86), 0);
    lv_obj_center(lbl);
}

void update(void) {}

void setTheme(int t) {
    s_theme = ((t % FEEDBACK_THEME_COUNT) + FEEDBACK_THEME_COUNT) % FEEDBACK_THEME_COUNT;
    if (s_themeCb) s_themeCb(s_theme);
}
int  theme() { return s_theme; }
void setThemeChangedCb(void (*cb)(int)) { s_themeCb = cb; }

void setRangeLabelVisible(bool) {}
void setSweepEnabled(bool)     {}
bool sweepEnabled()            { return false; }
void setAirportsEnabled(bool)  {}
bool airportsEnabled()         { return false; }
void setTrailLength(int)       {}
void setMaxOnScreen(int)       {}

} // namespace feedback_view
