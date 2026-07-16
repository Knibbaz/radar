// See feedback_log.h for the contract. This file is the implementation.
//
// On the device we use Preferences (NVS) + FreeRTOS queues + HTTPClient; on
// the native simulator all of those are stubbed out and the only persistent
// surface is ./feedback.csv (opened via stdio, fails silently if missing).

// Arduino headers (HTTPClient, WiFi) must be included OUTSIDE the feedback_log
// namespace: they pull in <memory> which uses unqualified std:: names, and
// inside `namespace feedback_log` those would resolve to feedback_log::std::*.
#include "feedback_log.h"
#include "config.h"
#include "feedback_view.h"            // applySettings() pushes through view setters

#include <stdio.h>
#include <string.h>
#include <ctime>                       // struct tm (C++ header; <time.h> doesn't always expose it)
#include <cstring>                     // memcpy

#if defined(ESP_PLATFORM)
#  include <Arduino.h>
#  include <Preferences.h>
#  include <HTTPClient.h>
#  include <WiFi.h>
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#  define FB_LOG(...)  Serial.printf(__VA_ARGS__)
#  define FB_PATH      "/sdcard/feedback.csv"     // ESP32 SD library default mount
#else
#  define FB_LOG(...)  fprintf(stderr, __VA_ARGS__)
#  define FB_PATH      "./feedback.csv"
#endif

namespace feedback_log {

// =========================================================================
// RAM ring buffer (capacity 500)
// =========================================================================
static const int RING_N = 500;
static Event s_ring[RING_N];
static volatile int s_ringHead  = 0;   // next write slot
static volatile int s_ringCount = 0;   // up to RING_N

static void ring_push(const Event &e) {
    int slot = s_ringHead;
    s_ring[slot] = e;
    s_ringHead = (slot + 1) % RING_N;
    if (s_ringCount < RING_N) ++s_ringCount;
}

int recentCopy(Event *out, int max) {
    const int n = (max < s_ringCount) ? max : s_ringCount;
    int newest = (s_ringHead - 1 + RING_N) % RING_N;
    for (int i = 0; i < n; ++i) {
        int slot = (newest - i + RING_N * 2) % RING_N;
        out[i] = s_ring[slot];
    }
    return n;
}

// =========================================================================
// Daily aggregates (31-day rolling, write-throttled to NVS)
// =========================================================================
struct PersistentDay {
    uint16_t year;
    uint8_t  mon, day;
    uint32_t good, neutral, bad;
};
static const int DAYS_N = 31;
static PersistentDay s_days[DAYS_N];
static int s_daysHead  = 0;   // index of the OLDEST entry
static int s_daysCount = 0;   // # of valid entries (<= DAYS_N)

static int find_today(uint16_t y, uint8_t m, uint8_t d) {
    for (int i = 0; i < s_daysCount; ++i) {
        int slot = (s_daysHead + i) % DAYS_N;
        const PersistentDay &pd = s_days[slot];
        if (pd.year == y && pd.mon == m && pd.day == d) return slot;
    }
    return -1;
}

static void bump_today_answer(uint8_t ans) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    uint16_t y = (uint16_t)(t.tm_year + 1900);
    uint8_t  m = (uint8_t)(t.tm_mon + 1);
    uint8_t  d = (uint8_t)t.tm_mday;
    int slot = find_today(y, m, d);
    if (slot < 0) {
        // new day -- if at capacity, evict the oldest before appending
        if (s_daysCount == DAYS_N) {
            s_daysHead = (s_daysHead + 1) % DAYS_N;
            --s_daysCount;
        }
        slot = (s_daysHead + s_daysCount) % DAYS_N;
        s_days[slot] = PersistentDay{y, m, d, 0, 0, 0};
        ++s_daysCount;
    }
    PersistentDay &pd = s_days[slot];
    if      (ans == 0) ++pd.good;
    else if (ans == 1) ++pd.neutral;
    else               ++pd.bad;
}

DayCount day(int day_back) {
    DayCount out{0, 0, 0, 0, 0, 0};
    if (day_back < 0 || day_back >= s_daysCount) return out;
    int slot = (s_daysHead + s_daysCount - 1 - day_back + DAYS_N * 2) % DAYS_N;
    const PersistentDay &pd = s_days[slot];
    out.year = pd.year; out.mon = pd.mon; out.day = pd.day;
    out.good = pd.good; out.neutral = pd.neutral; out.bad = pd.bad;
    return out;
}

// =========================================================================
// CSV append (silently fails when the file/path doesn't exist)
// =========================================================================
static const char *ans_label[3] = {"good", "neutral", "bad"};

static void csv_append(const Event &e) {
    FILE *f = fopen(FB_PATH, "a");
    if (!f) return;
    time_t t = (time_t)e.ts;
    struct tm g;
    gmtime_r(&t, &g);
    uint8_t a = (e.answer < 3) ? e.answer : 0;
    fprintf(f, "%04d-%02d-%02dT%02d:%02d:%02dZ,%s\n",
            g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
            g.tm_hour, g.tm_min, g.tm_sec, ans_label[a]);
    fclose(f);
}

// =========================================================================
// Optional webhook (device only -- low-priority FreeRTOS task)
// =========================================================================
#if defined(ESP_PLATFORM)

static QueueHandle_t s_q    = nullptr;
static TaskHandle_t  s_task = nullptr;
static char          s_webhookUrl[128] = {0};

static void webhook_task(void *) {
    Event e;
    while (true) {
        if (xQueueReceive(s_q, &e, pdMS_TO_TICKS(500)) != pdTRUE) continue;
        if (!s_webhookUrl[0] || WiFi.status() != WL_CONNECTED) continue;   // skip silently offline
        HTTPClient hc;
        hc.begin(s_webhookUrl);
        hc.setTimeout(3000);                                                // 3 s, per spec
        hc.addHeader("Content-Type", "application/json");
        char body[96];
        uint8_t a = (e.answer < 3) ? e.answer : 0;
        snprintf(body, sizeof(body), "{\"ts\":%lu,\"answer\":\"%s\"}",
                 (unsigned long)e.ts, ans_label[a]);
        const int code = hc.POST((uint8_t *)body, strlen(body));
        hc.end();
        (void)code;                                                         // single-shot, no retry
    }
}

static void webhook_start() {
    if (!s_webhookUrl[0] || s_q) return;
    s_q = xQueueCreate(16, sizeof(Event));
    if (!s_q) return;
    xTaskCreatePinnedToCore(webhook_task, "fbhook", 4096, nullptr, 1, &s_task, 0);
}

void setWebhookUrl(const char *url) {
    if (!url) { s_webhookUrl[0] = 0; return; }
    strncpy(s_webhookUrl, url, sizeof(s_webhookUrl) - 1);
    s_webhookUrl[sizeof(s_webhookUrl) - 1] = 0;
    webhook_start();
}
#else
static char s_webhookUrl[128] = {0};
void setWebhookUrl(const char *url) {
    if (!url) { s_webhookUrl[0] = 0; return; }
    strncpy(s_webhookUrl, url, sizeof(s_webhookUrl) - 1);
    s_webhookUrl[sizeof(s_webhookUrl) - 1] = 0;
    if (s_webhookUrl[0]) FB_LOG("[fbhook] (sim) webhook '%s' configured -- ignored in sim\n", s_webhookUrl);
}
#endif

static void webhook_enqueue(const Event &e) {
#if defined(ESP_PLATFORM)
    if (s_q) {
        Event copy = e;
        if (xQueueSend(s_q, &copy, 0) != pdTRUE) {
            // queue full -> drop, per spec ("never block UI")
        }
    }
#endif
}

// =========================================================================
// NVS persistence (throttled: max once per FEEDBACK_FLUSH_INTERVAL_S,
// or on explicit flush() -- e.g. on QR state change)
// =========================================================================
static bool s_flushPending = false;
#if defined(ESP_PLATFORM)
static Preferences s_nvs;
static uint32_t s_lastFlushAttemptMs = 0;
static constexpr const char *NVS_NS  = "feedback_log";
static constexpr const char *NVS_KEY = "v1_days";

static void nvs_load() {
    s_nvs.begin(NVS_NS, true);
    size_t got = s_nvs.getBytesLength(NVS_KEY);
    if (got == sizeof(s_days)) {
        s_nvs.getBytes(NVS_KEY, s_days, sizeof(s_days));
        // on-disk blob is contiguous starting at slot 0; rebuild head/count
        s_daysHead = 0;
        s_daysCount = 0;
        for (int i = 0; i < DAYS_N; ++i) {
            if (s_days[i].year != 0) s_daysCount = i + 1;
        }
        if (s_daysCount > DAYS_N) s_daysCount = DAYS_N;
    } else {
        s_daysHead = s_daysCount = 0;
    }
    s_nvs.end();
}

static void nvs_flush() {
    if (!s_nvs.begin(NVS_NS, false)) return;
    s_nvs.putBytes(NVS_KEY, s_days, sizeof(s_days));
    s_nvs.end();
    s_lastFlushAttemptMs = millis();
}
#else
static void nvs_load() { /* sim: no NVS */ }
static void nvs_flush() { /* sim: no NVS -- the CSV is the persistent record */ }
#endif

static void flush_throttled() {
    if (!s_flushPending) return;
#if defined(ESP_PLATFORM)
    const uint32_t now = millis();
    if (now - s_lastFlushAttemptMs < (uint32_t)FEEDBACK_FLUSH_INTERVAL_S * 1000UL) return;
#endif
    nvs_flush();
    s_flushPending = false;
}

void flush() {
    if (!s_flushPending) return;
    nvs_flush();
    s_flushPending = false;
}

void begin() {
    nvs_load();
    FB_LOG("[fb] log begin (ring %d events, %d day(s) cached)\n", RING_N, s_daysCount);
}

void loop() { flush_throttled(); }

void record(uint8_t answer) {
    if (answer > 2) answer = 0;
    const time_t ts = time(nullptr);
    const Event e{(uint32_t)ts, answer, {0, 0, 0}};
    ring_push(e);
    bump_today_answer(answer);
    csv_append(e);
    webhook_enqueue(e);
    s_flushPending = true;
    FB_LOG("[fb] %s @ %lu (ring %d/%d)\n",
           ans_label[answer], (unsigned long)ts, (int)s_ringCount, RING_N);
}

// =========================================================================
// Operator settings (NVS namespace "kiosk")
// =========================================================================
#if defined(ESP_PLATFORM)
static void cpy(char *dst, size_t cap, const String &src) {
    if (!dst || cap == 0) return;
    const int n = src.length();
    const int m = (n < (int)cap - 1) ? n : (int)cap - 1;
    memcpy(dst, src.c_str(), m);
    dst[m] = 0;
}
#endif

void loadSettings(Settings &out) {
#if defined(ESP_PLATFORM)
    Preferences p;
    p.begin("kiosk", true);
    out.mode        = p.getUChar ("mode",     FEEDBACK_DEFAULT_MODE);
    out.cooldownMs  = p.getUShort("cooldown", FEEDBACK_DEFAULT_COOLDOWN);
    cpy(out.question,    sizeof(out.question),    p.getString("q",       FEEDBACK_QUESTION_DEF));
    cpy(out.urlReview,   sizeof(out.urlReview),   p.getString("urlrev",  FEEDBACK_URL_REVIEW_DEF));
    cpy(out.urlInternal, sizeof(out.urlInternal), p.getString("urlin",   FEEDBACK_URL_INTERNAL_DEF));
    cpy(out.webhookUrl,  sizeof(out.webhookUrl),  p.getString("webhook", FEEDBACK_WEBHOOK_DEF));
    p.end();
#else
    out.mode        = FEEDBACK_DEFAULT_MODE;
    out.cooldownMs  = FEEDBACK_DEFAULT_COOLDOWN;
    snprintf(out.question,    sizeof(out.question),    "%s", FEEDBACK_QUESTION_DEF);
    snprintf(out.urlReview,   sizeof(out.urlReview),   "%s", FEEDBACK_URL_REVIEW_DEF);
    snprintf(out.urlInternal, sizeof(out.urlInternal), "%s", FEEDBACK_URL_INTERNAL_DEF);
    snprintf(out.webhookUrl,  sizeof(out.webhookUrl),  "%s", FEEDBACK_WEBHOOK_DEF);
#endif
}

void saveSettings(const Settings &s) {
#if defined(ESP_PLATFORM)
    Preferences p;
    p.begin("kiosk", false);
    p.putUChar ("mode",     s.mode);
    p.putUShort("cooldown", s.cooldownMs);
    p.putString("q",       s.question);
    p.putString("urlrev",  s.urlReview);
    p.putString("urlin",   s.urlInternal);
    p.putString("webhook", s.webhookUrl);
    p.end();
#else
    (void)s;   // sim: settings live for the process only
#endif
}

void applySettings(const Settings &s) {
    // clamp before pushing so a stray /save?cooldown=99999 can't break the cadence
    const uint16_t cd = (s.cooldownMs < 2000) ? 2000 : (s.cooldownMs > 30000 ? 30000 : s.cooldownMs);
    const uint8_t  md = (s.mode > 1) ? FEEDBACK_DEFAULT_MODE : s.mode;

    feedback_view::setMode       ((int)md);
    feedback_view::setQuestion   (s.question);
    feedback_view::setUrlReview  (s.urlReview);
    feedback_view::setUrlInternal(s.urlInternal);
    feedback_view::setCooldownMs ((int)cd);
    setWebhookUrl                (s.webhookUrl);
}

void buildHeatGrid(HeatGrid &out) {
    memset(&out, 0, sizeof(out));
    Event ev[500];
    const int n = recentCopy(ev, 500);
    for (int i = 0; i < n; ++i) {
        time_t t = (time_t)ev[i].ts;
        struct tm g; gmtime_r(&t, &g);
        const int wd = (g.tm_wday < 0 || g.tm_wday > 6) ? 0 : g.tm_wday;
        const int hr = g.tm_hour;
        const int dp = (hr >= 6 && hr < 12) ? 0
                  : (hr >= 12 && hr < 18) ? 1
                  : 2;                                                  // evening 18-24 wraps to 2 too
        out.cell[wd][dp].total++;
        if (ev[i].answer == 0) out.cell[wd][dp].good++;
    }
}

void build31DaySeries(DaySeries &out) {
    memset(&out, 0, sizeof(out));
    out.count = 0;
    // day(day_back) is 0=today, 1=yesterday, ...; we want oldest-first.
    for (int back = (int)FEEDBACK_DAYS_N - 1; back >= 0; --back) {
        const DayCount d = day(back);
        const int slot = out.count;
        if (slot >= (int)FEEDBACK_DAYS_N) break;
        out.year  [slot] = d.year;
        out.mon   [slot] = d.mon;
        out.day   [slot] = d.day;
        out.good  [slot] = d.good;
        out.neutral[slot] = d.neutral;
        out.bad   [slot] = d.bad;
        out.count++;
    }
}

} // namespace feedback_log
