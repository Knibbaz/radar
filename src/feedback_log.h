#pragma once
// Anonymous event log for the feedback kiosk.
//
// An "event" is exactly: (unix timestamp, answer). No IDs, no network metadata
// (privacy by design -- nothing personal is stored anywhere). The answer is
// 0 = good, 1 = neutral, 2 = bad.
//
// Four backends, all driven by record():
//   1. RAM ring buffer of the last 500 events (cheap, no I/O).
//   2. Per-day totals for the last 31 days, rolling.  Write-throttled to NVS:
//      flushed at most once per FEEDBACK_FLUSH_INTERVAL_S, or on flush().
//   3. CSV append to /sdcard/feedback.csv on device, ./feedback.csv in the
//      native sim.  fopen failures are silent ("no card" == no CSV).
//   4. Optional webhook (device only): FreeRTOS task with a 16-slot queue.
//      Single POST per event, 3 s timeout, no retry, drops on offline.
//
// begin() / loop() / record() / flush() are all safe to call from core 1
// (the LVGL thread that runs feedback_view's tap handler).

#include <stdint.h>

namespace feedback_log {

// ring buffer / daily-aggregate sizes
static const int FEEDBACK_RING_N   = 500;     // exposed for tests / docs
static const int FEEDBACK_DAYS_N   = 31;      // last-31-day rolling window

struct Event {
    uint32_t ts;        // unix epoch (RTC/NTP-synced on device, host clock in sim)
    uint8_t  answer;    // 0 = good, 1 = neutral, 2 = bad
    uint8_t  _pad[3];
};

struct DayCount {
    uint16_t year;
    uint8_t  mon, day;
    uint32_t good, neutral, bad;
};

// 0 = REVIEW (default): show QR codes after the smiley tap.
// 1 = DASHBOARD: after THANKS go straight back to IDLE, never show any QR.
enum FeedbackMode { FB_MODE_REVIEW = 0, FB_MODE_DASHBOARD = 1 };

// Operator-tunable settings, persisted in NVS under namespace "kiosk".
// Live edits push through feedback_view (URL -> QR refresh, etc.) and the
// log backend (webhook URL); the fields are kept short so the NVS blob is
// small and stable across upgrades.
struct Settings {
    uint8_t  mode;                              // FeedbackMode
    uint16_t cooldownMs;                        // 2000..30000 ms
    char     question    [64];
    char     urlReview   [128];
    char     urlInternal [128];
    char     webhookUrl  [128];
};

void begin();                                         // load NVS daily counts
void loop();                                          // drive throttled NVS flush
void record(uint8_t answer);                          // 0=good 1=neutral 2=bad; never blocks
void flush();                                         // force-write throttled state now

int      recentCopy(Event *out, int max);             // latest first; returns count copied
DayCount day(int day_back);                           // 0 = today, 1 = yesterday, ... {0,...} if absent

void setWebhookUrl(const char *url);                  // "" = disabled

// Forward declarations so the function signatures below compile even when this
// header is included by a TU that only needs the API (e.g. stats_html.h).
struct Settings;
struct HeatCell;
struct HeatGrid;
struct DaySeries;

// ----- operator settings (NVS namespace "kiosk") -----
void loadSettings(Settings &out);                     // fill from NVS or compile-time defaults
void saveSettings(const Settings &s);                 // persist to NVS
void applySettings(const Settings &s);                // push into feedback_view (live) + setWebhookUrl()

// ----- dashboard aggregations (read-only helpers) -----
// Heat table: weekday (0=Sun..6=Sat) x daypart (0=morning 06-12, 1=afternoon 12-18, 2=evening 18-24).
struct HeatCell { uint32_t good = 0, total = 0; };
struct HeatGrid { HeatCell cell[7][3]; };
void buildHeatGrid(HeatGrid &out);                    // walks the 500-event ring buffer

// Last-31-days series, oldest first, today last. Used by /stats and /api/stats.
struct DaySeries {
    uint16_t year  [31];
    uint8_t  mon   [31];
    uint8_t  day   [31];
    uint32_t good  [31];
    uint32_t neutral[31];
    uint32_t bad   [31];
    uint8_t  count;                                    // number of valid slots (<= 31)
};
void build31DaySeries(DaySeries &out);

} // namespace feedback_log
