#pragma once
// Customer-feedback kiosk view: LOGO -> CHOICE -> POP -> QR -> LOGO.
//
// State machine:
//   LOGO   idle dim screen with logo/placeholder, drift burn-in protection
//   CHOICE green/red split screen, 2 smileys (good/bad), 10s timeout
//   POP    expanding circle animation in chosen color (~400ms)
//   QR     QR code on white tile, centered on black. Tap/timeout -> LOGO
//
// ui.cpp wires the top HUD on top of this; CHOICE hides it.

#include <lvgl.h>

// What the user rated. Values match feedback_log encoding (0=good, 1=bad).
enum FeedbackAnswer {
    FEEDBACK_GOOD = 0,
    FEEDBACK_BAD  = 1
};

// Theme enums (kept for web config plumbing — no visual effect in kiosk mode)
enum FeedbackTheme {
    FEEDBACK_THEME_PHOSPHOR = 0,
    FEEDBACK_THEME_ORB      = 1,
    FEEDBACK_THEME_AMBER    = 2,
    FEEDBACK_THEME_MILITARY = 3,
    FEEDBACK_THEME_COUNT    = 4
};

namespace feedback_view {

// Theme API (no-op in kiosk, kept for web config NVS round-trip)
void setTheme(int theme);
int  theme();
void setThemeChangedCb(void (*cb)(int theme));

// Build all state surfaces as children of `lv_parent`. Safe to call once.
void init(void* lv_parent);

// Logo loading (SPIFFS on device, local file on sim). Call after init().
void loadLogo(void);
void removeLogo(void);

// Live operator setters (called from feedback_log::applySettings() and the
// web config handlers). All safe to call from any state.
void setMode(int mode);                  // 0 = REVIEW (show QR), 1 = DASHBOARD (skip QR)
void setQuestion(const char *text);      // update LOGO placeholder label
void setUrlReview(const char *url);      // re-render the QR widget
void setUrlInternal(const char *url);    // (kept for API compat, unused in 2-answer flow)
void setCooldownMs(int ms);              // tap-absorb window after returning to LOGO
void setIdleBrightness(int pct);         // 0..100, LOGO dim level

// Admin overlay (LOGO long-press 3 s). Read-only — config edits go through web page.
void setAdminVisible(bool on);
void setAdminIp(const char *ip);
void setAdminToday(uint32_t good, uint32_t bad);

} // namespace feedback_view