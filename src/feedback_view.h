#pragma once
// Full-screen placeholder view for the customer-feedback terminal.
// Stands in where the radar scope used to live: ui.cpp wires up the top HUD
// on top of this content. The actual smiley / rating UI will replace this.
#include <lvgl.h>

// Theme indices (kept in sync with the web config page's <select> options).
enum FeedbackTheme {
    FEEDBACK_THEME_PHOSPHOR = 0,
    FEEDBACK_THEME_ORB      = 1,
    FEEDBACK_THEME_AMBER    = 2,
    FEEDBACK_THEME_MILITARY = 3,
    FEEDBACK_THEME_COUNT    = 4
};

namespace feedback_view {

// Build the placeholder (a centered "FEEDBACK KIOSK" label on true black).
void init(void* lv_parent);

// Reserved for future data-driven updates; no-op for now.
void update(void);

// Theme: no-op visually (the HUD palette is owned by ui.cpp), but the value
// round-trips through the change callback so the web config page and NVS
// still see a consistent theme index.
void setTheme(int theme);
int  theme();
void setThemeChangedCb(void (*cb)(int theme));

// The radar had several "do nothing visible" knobs (sweep line, airport markers,
// trail length, max-on-screen, range label). They map to no-ops here — the web
// config still saves them to NVS, just nothing renders them.
void setRangeLabelVisible(bool v);
void setSweepEnabled(bool on);
bool sweepEnabled();
void setAirportsEnabled(bool on);
bool airportsEnabled();
void setTrailLength(int level);
void setMaxOnScreen(int n);

} // namespace feedback_view
