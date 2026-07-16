#pragma once
// Customer-feedback kiosk view.
//
// Four-state machine layered on top of LVGL v8:
//
//   IDLE      question text + three large smileys (GOOD / NEUTRAL / BAD)
//   THANKS    full-screen green panel with check mark + "Bedankt"  (~1.5s)
//   QR        review QR (GOOD) or internal form QR + smaller public QR
//             (NEUTRAL/BAD). Auto-returns to IDLE after ~12s, or on tap.
//   COOLDOWN  progress arc on the screen edge while taps are absorbed,
//             to keep rapid-fire taps from polluting the rating stats.
//
// ui.cpp wires the top HUD (WiFi / clock / battery / date) up on top of
// this content. The radar-era knobs (sweep, airports, trail, etc.) are
// kept as no-op stubs so the web config plumbing still round-trips through
// NVS without changing the call sites in main.cpp.
#include <lvgl.h>

enum FeedbackTheme {
    FEEDBACK_THEME_PHOSPHOR = 0,
    FEEDBACK_THEME_ORB      = 1,
    FEEDBACK_THEME_AMBER    = 2,
    FEEDBACK_THEME_MILITARY = 3,
    FEEDBACK_THEME_COUNT    = 4
};

// What the user rated. Logged on each tap (timestamp comes from time()).
// Persistence / stats aggregation is layered on top in a later step.
enum FeedbackAnswer {
    FEEDBACK_GOOD    = 1,
    FEEDBACK_NEUTRAL = 2,
    FEEDBACK_BAD     = 3
};

namespace feedback_view {

// Build all four state surfaces as children of `lv_parent`. Safe to call
// once; a second call is a no-op.
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

// Live operator setters (called from feedback_log::applySettings() and the
// web config handlers). All safe to call from any state; they take effect
// the next time that surface is shown.
void setMode(int mode);                  // 0 = REVIEW, 1 = DASHBOARD (no QR)
void setQuestion(const char *text);      // update the IDLE label
void setUrlReview(const char *url);      // re-render the GOOD QR widget
void setUrlInternal(const char *url);    // re-render the NEUTRAL/BAD QR widget
void setCooldownMs(int ms);              // applied to the next cooldown animation

// Admin overlay (IDLE long-press 3 s). All data shown read-only -- config
// edits go through the web page at http://feedback.local/.
void setAdminVisible(bool on);           // show / hide the admin overlay
void setAdminIp(const char *ip);         // IP-address string (for the config URL)
void setAdminToday(uint32_t good, uint32_t neutral, uint32_t bad);  // today's counts

} // namespace feedback_view
