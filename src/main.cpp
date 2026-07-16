// Capsule Radar — entry point / glue. SKELETON: TODOs mark what to implement.
// Order of work is in CLAUDE.md (milestones). Bring up the Waveshare demo first.
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "geo.h"                     // GPS distance check (haversine)
#include "feedback_view.h"           // placeholder main canvas (was radar_view)
#include "feedback_log.h"            // anonymous event log (ring + NVS counters + CSV + webhook)
#include "stats_html.h"              // /stats + /api/stats renderers
#include "ui.h"
#include "display.h"                  // M0: CO5300 + LVGL bring-up
#include "imu_qmi8658.h"             // face-down sleep
#include "gps.h"                     // LC76G GNSS (-G variant only)
#include "battery.h"                 // AXP2101 battery gauge
#include "rtc_pcf85063.h"            // PCF85063 RTC (offline clock + date)
#include "audio.h"                   // ES8311 audio (alert pings later)
#include <string>
#include <WiFiManager.h>             // captive portal
#include <Preferences.h>            // NVS (persist theme/settings)
#include <time.h>                   // NTP/RTC clock + date
#include <WebServer.h>              // configuration web page
#include <ESPmDNS.h>                // http://capsuleradar.local
#include <ArduinoOTA.h>             // OTA firmware update over WiFi (PlatformIO/espota)
#include <Update.h>                 // browser OTA: self-flash an uploaded .bin
#include <esp_heap_caps.h>          // largest-free-block metric (heap health)
#include <esp_wifi.h>               // WiFi driver control (reset must survive the reboot)
#include <nvs.h>                    // erase the driver's "nvs.net80211" namespace (WiFi reset)

// ---- shared state ----
static WiFiManager           g_wm;
static int                   g_brightnessDay = BRIGHTNESS_DEFAULT;   // user brightness (web/NVS)
static int                   g_volume = 60;                          // alert volume 0..100 (web/NVS)
static bool                  g_muted  = false;                       // mute alert pings
static int                   g_alertMode = 2;                        // 0=off 1=emergencies 2=new+emergencies (web/NVS)
static float                 g_proximityKm = 0.0f;                   // proximity alert radius, km (0=off) (web/NVS)
static uint32_t              g_idleDimMs = IDLE_DIM_MS;              // dim after this idle time (0 = never)
static bool                  g_showSweep = true;                     // rotating sweep line on/off (web/NVS)
static int                   g_units = 0;                            // 0=Aviation 1=Metric 2=Imperial (web/NVS)
static bool                  g_showAirports = true;                  // airport markers on/off (web/NVS)
static bool                  g_hideGround   = false;                 // skip on-ground aircraft in the feed (web/NVS)
static int                   g_minAltFt     = 0;                     // only show aircraft above this altitude, ft (0 = off) (web/NVS)
static bool                  g_milOnly      = false;                 // only show military-flagged aircraft (web/NVS)
static int                   g_rotation = 0;                         // display rotation 0/1/2/3 = 0/90/180/270 (web/NVS)
static bool                  g_useGps = false;                       // auto-set home from the LC76G GPS (-G variant) (web/NVS)
static int                   g_trailLen = 2;                         // aircraft trails 0=off 1=short 2=med 3=long (web/NVS)
static int                   g_maxAc = 20;                           // max aircraft drawn on the scope (web/NVS)
static volatile bool         g_onBattery = false;                    // discharging (set on core 1, read on core 0)
static bool                  g_rtcSynced = false;                    // RTC written from NTP this session?
static volatile uint32_t     g_rebootAtMs = 0;                       // !=0: reboot when millis() reaches it (clean start after WiFi config)
static String                g_tz = TZ_STR;                          // POSIX timezone (web-configurable, NVS); applied via configTzTime

// Web-selectable time zones (label + POSIX TZ). The <option> value is the index; the save
// handler maps it back to the POSIX string stored in NVS and used by configTzTime at boot.
// (Index avoids putting POSIX strings with '<>' / ',' into HTML attributes.)
// offMin = standard (winter) UTC offset in minutes; dst = 1 if the zone observes DST.
// The web page uses these to auto-pick the visitor's zone from their browser clock.
static const struct { const char *label; const char *tz; int offMin; int dst; } TZOPTS[] = {
    {"UTC",                      "UTC0",                              0, 0},
    {"London / Lisbon",          "GMT0BST,M3.5.0/1,M10.5.0",          0, 1},
    {"Madrid / Paris / Berlin",  "CET-1CEST,M3.5.0,M10.5.0/3",       60, 1},
    {"Athens / Helsinki",        "EET-2EEST,M3.5.0/3,M10.5.0/4",     120, 1},
    {"New York (US Eastern)",    "EST5EDT,M3.2.0,M11.1.0",          -300, 1},
    {"Chicago (US Central)",     "CST6CDT,M3.2.0,M11.1.0",          -360, 1},
    {"Denver (US Mountain)",     "MST7MDT,M3.2.0,M11.1.0",          -420, 1},
    {"Phoenix (Arizona)",        "MST7",                            -420, 0},
    {"Los Angeles (US Pacific)", "PST8PDT,M3.2.0,M11.1.0",          -480, 1},
    {"Anchorage (Alaska)",       "AKST9AKDT,M3.2.0,M11.1.0",        -540, 1},
    {"Honolulu (Hawaii)",        "HST10",                           -600, 0},
    {"Argentina / Brazil (E)",   "<-03>3",                          -180, 0},
    {"India (IST)",              "<+0530>-5:30",                     330, 0},
    {"China / Singapore",        "<+08>-8",                          480, 0},
    {"Japan / Korea",            "JST-9",                            540, 0},
    {"Sydney (AU Eastern)",      "AEST-10AEDT,M10.1.0,M4.1.0/3",     600, 1},
    {"Auckland (NZ)",            "NZST-12NZDT,M9.5.0,M4.1.0/3",      720, 1},
};
static const int TZOPTS_N = sizeof(TZOPTS) / sizeof(TZOPTS[0]);


static void loadSettings() {
    Preferences p;
    p.begin("capsuleradar", true);
    g_brightnessDay    = p.getInt("bright", BRIGHTNESS_DEFAULT);
    g_volume           = p.getInt("vol", 60);
    g_muted            = p.getBool("mute", false);
    g_alertMode        = p.getInt("alertmode", 2);
    g_proximityKm      = p.getFloat("proxkm", 0.0f);
    g_useGps           = p.getBool("usegps", false);
    g_trailLen         = p.getInt("traillen", 2);
    g_maxAc            = p.getInt("maxac", 20);
    g_idleDimMs        = p.getUInt("idledim", IDLE_DIM_MS);
    g_units            = p.getInt("units", 0);
    g_tz               = p.getString("tz", TZ_STR);
    p.end();
}



// Persist the visual theme in NVS (called when the user long-presses to switch).
static void saveTheme(int t) {
    Preferences p;
    p.begin("capsuleradar", false);
    p.putInt("theme", t);
    p.end();
}

// Convert a UTC broken-down time to time_t (mktime assumes local TZ, so flip to UTC0).
static time_t utc_to_time(struct tm *utc) {
    setenv("TZ", "UTC0", 1); tzset();
    const time_t t = mktime(utc);
    setenv("TZ", TZ_STR, 1); tzset();   // restore local TZ for getLocalTime()
    return t;
}

// Seed the ESP system clock from the RTC so the clock/date are right before NTP.
static void rtc_seed_clock() {
    struct tm utc;
    if (!rtc_read(&utc)) { Serial.println("[rtc] no valid time stored"); return; }
    const time_t t = utc_to_time(&utc);
    struct timeval tv = { t, 0 };
    settimeofday(&tv, nullptr);
    Serial.println("[rtc] system clock seeded from RTC");
}

// Brightness combines idle auto-dim and face-down sleep (sleep wins -> screen off).
static bool g_asleep = false;   // face-down
static bool g_idle   = false;   // no touch for a while
static void applyBrightness() {
    int b = g_brightnessDay;
    if (g_idle  && BRIGHTNESS_IDLE  < b) b = BRIGHTNESS_IDLE;   // idle only dims down
    if (g_asleep) b = 0;                                         // face-down -> screen off
    display::setBrightness(b);
}

// ----------------------------- configuration web --------------------------------
static WebServer g_web(80);

static void handleRoot() {
    feedback_log::Settings st;
    feedback_log::loadSettings(st);

    char q[sizeof(st.question)], rev[sizeof(st.urlReview)], rin[sizeof(st.urlInternal)], whk[sizeof(st.webhookUrl)];
    snprintf(q,   sizeof(q),   "%s", st.question);
    snprintf(rev, sizeof(rev), "%s", st.urlReview);
    snprintf(rin, sizeof(rin), "%s", st.urlInternal);
    snprintf(whk, sizeof(whk), "%s", st.webhookUrl);
    const int  cd_s = st.cooldownMs / 1000;

    static const size_t BUFSZ = 8192;
    static char *buf = (char *)ps_malloc(BUFSZ);   // PSRAM: keep this page off the scarce internal heap
    if (!buf) return;

    snprintf(buf, BUFSZ,
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Feedback Kiosk - Configuration</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{background:radial-gradient(circle at 50%% -10%%,#0a1f15,#04100a 70%%);color:#cdd6d1;"
        "font-family:system-ui,-apple-system,sans-serif;margin:0 auto;padding:20px;max-width:520px;min-height:100vh}"
        ".hd{display:flex;align-items:center;gap:12px;margin-bottom:16px}"
        ".dot{width:44px;height:44px;border-radius:50%%;border:2px solid #1dff86;flex:0 0 auto;"
        "box-shadow:0 0 16px rgba(29,255,134,.4)}"
        "h1{color:#1dff86;font-size:20px;margin:0}.sub{color:#6f8c7d;font-size:12px;margin:2px 0 0}"
        ".t{color:#1dff86;font-size:11px;letter-spacing:1.5px;text-transform:uppercase;margin-bottom:10px;opacity:.85}"
        "label{display:block;margin:12px 0 4px;color:#9affc8;font-size:13px}"
        "input,select{width:100%%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #2a4a39;"
        "background:#0c1a12;color:#eafff3;font-size:16px}"
        "input:focus,select:focus{outline:none;border-color:#1dff86;box-shadow:0 0 0 2px rgba(29,255,134,.18)}"
        "button{margin-top:16px;width:100%%;padding:12px;border:0;border-radius:8px;background:#1dff86;"
        "color:#04140b;font-weight:700;font-size:16px}button:active{opacity:.85}"
        ".card{background:rgba(10,20,14,.85);border:1px solid #1f3a2b;border-radius:14px;padding:16px;margin-bottom:14px}"
        ".ft{color:#5f7a6c;font-size:12px;text-align:center;margin-top:10px}.ft code{color:#9affc8}"
        ".w{background:#ffb23c}"
        "</style></head><body>"
        "<div class=hd><div class=dot></div><div><h1>Feedback Kiosk</h1>"
        "<p class=sub>Customer feedback terminal &middot; configuration</p></div></div>"

        // ----- Kiosk settings (persisted in NVS) -----
        "<div class=card><div class=t>Feedback flow</div><form method=POST action=/save>"
        "<label>Mode</label><select name=mode>"
          "<option value=0%s>Review (kapper) &mdash; QR funnel to public review</option>"
          "<option value=1%s>Dashboard (wachtkamer) &mdash; private only, no QR</option>"
        "</select>"
        "<label>Question shown on screen (max 60 chars)</label>"
        "<input name=q maxlength=60 value='%s' required>"
        "<label>Public review URL (Google Maps etc.)</label>"
        "<input name=urlrev type=url value='%s'>"
        "<label>Internal form URL</label>"
        "<input name=urlin type=url value='%s'>"
        "<label>Webhook URL (optional, JSON POST per tap)</label>"
        "<input name=webhook type=url value='%s'>"
        "<label>Cooldown between interactions: <span id='cds'>%d</span>&nbsp;s</label>"
        "<input type=range min=2 max=30 step=1 value='%d' name=cooldown oninput='cds.innerText=this.value'>"
        "<button>Save</button></form></div>"

        // ----- Display / brightness (live, in-session) -----
        "<div class=card><div class=t>Display</div>"
        "<label>Brightness</label>"
        "<input type=range min=5 max=255 value='%d' oninput='b(this.value,0)' onchange='b(this.value,1)'>"
        "<button type=button class=sec onclick=\"location.href='/stats'\">Open operator dashboard &rarr;</button>"
        "</div>"

        // ----- Network -----
        "<div class=card><div class=t>Network</div>"
        "<p style='color:#9affc8;font-size:13px;margin:0 0 4px'>Forget the saved WiFi and reopen the setup portal.</p>"
        "<form method=POST action=/wifi><button class=w>Reset WiFi</button></form></div>"

        "<p class=ft>Reach me at <code>feedback.local</code> &middot; <a href=/update style='color:#9affc8'>Firmware update</a> &middot; v" FW_VERSION "</p>"
        "<script>"
        "function b(v,s){fetch('/bright?v='+v+(s?'&save=1':''))}"
        "</script></body></html>",
        st.mode == 0 ? " selected" : "", st.mode == 1 ? " selected" : "",
        q, rev, rin, whk,
        cd_s, cd_s,
        g_brightnessDay);
    g_web.send(200, "text/html", buf);
}

static void handleSave() {
    feedback_log::Settings st;
    feedback_log::loadSettings(st);

    if (g_web.hasArg("mode")) {
        const int m = g_web.arg("mode").toInt();
        st.mode = (uint8_t)((m == 1) ? 1 : 0);
    }
    if (g_web.hasArg("q")) {
        String s = g_web.arg("q");
        s.trim();
        if (s.length() > FEEDBACK_MAX_QUESTION) s = s.substring(0, FEEDBACK_MAX_QUESTION);
        strncpy(st.question, s.c_str(), sizeof(st.question) - 1);
        st.question[sizeof(st.question) - 1] = 0;
    }
    if (g_web.hasArg("urlrev"))  { strncpy(st.urlReview,   g_web.arg("urlrev").c_str(),  sizeof(st.urlReview)   - 1); st.urlReview  [sizeof(st.urlReview)   - 1] = 0; }
    if (g_web.hasArg("urlin"))   { strncpy(st.urlInternal, g_web.arg("urlin").c_str(),   sizeof(st.urlInternal) - 1); st.urlInternal[sizeof(st.urlInternal) - 1] = 0; }
    if (g_web.hasArg("webhook")){ strncpy(st.webhookUrl,  g_web.arg("webhook").c_str(), sizeof(st.webhookUrl) - 1); st.webhookUrl [sizeof(st.webhookUrl)  - 1] = 0; }
    if (g_web.hasArg("cooldown")) {
        const long c = g_web.arg("cooldown").toInt();
        st.cooldownMs = (uint16_t)constrain((int)c, 2, 30) * 1000;
    }

    feedback_log::saveSettings(st);
    feedback_log::applySettings(st);   // live update (question/URLs/cooldown/webhook + mode)

    g_web.send(200, "text/html",
        "<meta http-equiv=refresh content='2;url=/'><body style='background:#06100a;color:#1dff86;"
        "font-family:sans-serif;padding:24px'>Saved.</body>");
}

static void handleStatsPage() {
    char buf[8192];
    const int n = stats_html::render_html(buf, sizeof(buf));
    if (n <= 0) { g_web.send(500, "text/plain", "render failed"); return; }
    g_web.send(200, "text/html", buf);
}

static void handleStatsApi() {
    char buf[3072];
    const int n = stats_html::render_json(buf, sizeof(buf));
    if (n <= 0) { g_web.send(500, "text/plain", "render failed"); return; }
    g_web.send(200, "application/json", buf);
}

// /webhook?v=<url>&save=1   (live; no /save needed)
static void handleWebhookUrl() {
    if (g_web.hasArg("v")) {
        feedback_log::Settings st;
        feedback_log::loadSettings(st);
        strncpy(st.webhookUrl, g_web.arg("v").c_str(), sizeof(st.webhookUrl) - 1);
        st.webhookUrl[sizeof(st.webhookUrl) - 1] = 0;
        feedback_log::setWebhookUrl(st.webhookUrl);
        if (g_web.hasArg("save")) { feedback_log::saveSettings(st); }
    }
    g_web.send(200, "text/plain", "ok");
}

// /mode?v=0|1&save=1
static void handleMode() {
    if (g_web.hasArg("v")) {
        feedback_log::Settings st;
        feedback_log::loadSettings(st);
        st.mode = (uint8_t)((g_web.arg("v").toInt() == 1) ? 1 : 0);
        feedback_view::setMode((int)st.mode);
        if (g_web.hasArg("save")) { feedback_log::saveSettings(st); }
    }
    g_web.send(200, "text/plain", "ok");
}

// /q?v=<text>&save=1  (live; truncated to FEEDBACK_MAX_QUESTION)
static void handleQuestion() {
    if (g_web.hasArg("v")) {
        feedback_log::Settings st;
        feedback_log::loadSettings(st);
        String s = g_web.arg("v");
        s.trim();
        if (s.length() > FEEDBACK_MAX_QUESTION) s = s.substring(0, FEEDBACK_MAX_QUESTION);
        strncpy(st.question, s.c_str(), sizeof(st.question) - 1);
        st.question[sizeof(st.question) - 1] = 0;
        feedback_view::setQuestion(st.question);
        if (g_web.hasArg("save")) { feedback_log::saveSettings(st); }
    }
    g_web.send(200, "text/plain", "ok");
}

// /urlrev?v=<url>&save=1   / /urlin?v=<url>&save=1   (live; refreshes LVGL QR widgets)
static void handleUrl() {
    if (g_web.hasArg("v")) {
        const bool internal = (g_web.uri() == "/urlin");
        feedback_log::Settings st;
        feedback_log::loadSettings(st);
        if (internal) {
            strncpy(st.urlInternal, g_web.arg("v").c_str(), sizeof(st.urlInternal) - 1);
            st.urlInternal[sizeof(st.urlInternal) - 1] = 0;
            feedback_view::setUrlInternal(st.urlInternal);
        } else {
            strncpy(st.urlReview, g_web.arg("v").c_str(), sizeof(st.urlReview) - 1);
            st.urlReview[sizeof(st.urlReview) - 1] = 0;
            feedback_view::setUrlReview(st.urlReview);
        }
        if (g_web.hasArg("save")) { feedback_log::saveSettings(st); }
    }
    g_web.send(200, "text/plain", "ok");
}

// /cooldown?v=2..30&save=1   (seconds)
static void handleCooldown() {
    if (g_web.hasArg("v")) {
        const long s = g_web.arg("v").toInt();
        const int clamped = (int)constrain((int)s, 2, 30);
        feedback_view::setCooldownMs(clamped * 1000);
        if (g_web.hasArg("save")) {
            feedback_log::Settings st;
            feedback_log::loadSettings(st);
            st.cooldownMs = (uint16_t)(clamped * 1000);
            feedback_log::saveSettings(st);
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleWifi() {
    g_web.send(200, "text/html",
        "<body style='background:#06100a;color:#ffb23c;font-family:sans-serif;padding:24px'>"
        "WiFi reset. Connect to the <b>Feedback-Setup</b> network to reconfigure.</body>");
    delay(400);                     // let the response reach the browser
    // The driver stores the saved AP in its own NVS namespace ("nvs.net80211"). On Arduino
    // core 3.x both wm.resetSettings() and WiFi.disconnect(true,true) can silently no-op
    // (they fail once the driver is off), so v1.3.19's reset still reconnected. Erasing that
    // namespace directly is unconditional — it works whatever state the WiFi driver is in.
    g_wm.resetSettings();           // best-effort driver-level erase first...
    WiFi.disconnect(false, true);   // ...keep WiFi up so the erase can actually run
    delay(100);
    nvs_handle_t h;                 // ...then the guaranteed path: wipe the driver's namespace
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    delay(300);                     // let NVS finish committing before the reboot
    ESP.restart();
}

static void handleBright() {
    if (g_web.hasArg("v")) {
        g_brightnessDay = constrain((int)g_web.arg("v").toInt(), 0, 255);
        applyBrightness();
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("bright", g_brightnessDay);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleVol() {
    if (g_web.hasArg("v"))    { g_volume = constrain((int)g_web.arg("v").toInt(), 0, 100); audio_set_volume(g_volume); }
    if (g_web.hasArg("mute")) { g_muted = g_web.arg("mute").toInt() != 0; audio_set_muted(g_muted); }
    if (g_web.hasArg("save")) {
        Preferences p;
        p.begin("capsuleradar", false);
        p.putInt("vol", g_volume);
        p.putBool("mute", g_muted);
        p.end();
    }
    if (g_web.hasArg("test")) {
        if (g_web.arg("test").toInt() == 2) audio_selftest();   // long tone, ignores mute
        else audio_play(AUDIO_NEW);
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleAlerts() {   // what triggers the alert sound (live)
    if (g_web.hasArg("mode")) g_alertMode   = constrain((int)g_web.arg("mode").toInt(), 0, 2);
    if (g_web.hasArg("prox")) g_proximityKm = g_web.arg("prox").toFloat();   // km (0 = off)
    if (g_web.hasArg("save")) {
        Preferences p;
        p.begin("capsuleradar", false);
        p.putInt("alertmode", g_alertMode);
        p.putFloat("proxkm", g_proximityKm);
        p.end();
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleIdle() {   // idle auto-dim timeout (seconds; 0 = never)
    if (g_web.hasArg("v")) {
        const long s = g_web.arg("v").toInt();
        g_idleDimMs = (s <= 0) ? 0 : (uint32_t)s * 1000;
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putUInt("idledim", g_idleDimMs);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleUnits() {   // measurement units preset (live re-render)
    if (g_web.hasArg("v")) {
        g_units = constrain((int)g_web.arg("v").toInt(), 0, 2);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("units", g_units);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleSweep() {   // show/hide the rotating sweep line (live)
    if (g_web.hasArg("v")) {
        g_showSweep = g_web.arg("v").toInt() != 0;
        feedback_view::setSweepEnabled(g_showSweep);  // no-op stub (radar-era knob)
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putBool("sweep", g_showSweep);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleTrail() {   // aircraft trail length 0/1/2/3 (live)
    if (g_web.hasArg("v")) {
        g_trailLen = constrain((int)g_web.arg("v").toInt(), 0, 3);
        feedback_view::setTrailLength(g_trailLen);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("traillen", g_trailLen);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleAltMin() {   // minimum-altitude feed filter, ft (applies from the next poll)
    if (g_web.hasArg("v")) {
        g_minAltFt = constrain((int)g_web.arg("v").toInt(), 0, 60000);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("minalt", g_minAltFt);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleMilOnly() {   // military-only feed filter (applies from the next poll)
    if (g_web.hasArg("v")) {
        g_milOnly = g_web.arg("v").toInt() != 0;
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putBool("milonly", g_milOnly);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleMaxAc() {   // max aircraft drawn on the scope (live)
    if (g_web.hasArg("v")) {
        g_maxAc = constrain((int)g_web.arg("v").toInt(), 1, ADSB_MAX_AIRCRAFT);
        feedback_view::setMaxOnScreen(g_maxAc);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("maxac", g_maxAc);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleAirports() {   // show/hide airport markers (live)
    if (g_web.hasArg("v")) {
        g_showAirports = g_web.arg("v").toInt() != 0;
        feedback_view::setAirportsEnabled(g_showAirports);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putBool("airports", g_showAirports);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleGround() {   // hide/show on-ground aircraft (applies from the next feed poll)
    if (g_web.hasArg("v")) {
        g_hideGround = g_web.arg("v").toInt() != 0;
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putBool("hideground", g_hideGround);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleRotate() {   // display rotation 0/90/180/270 for any USB-C orientation (live)
    if (g_web.hasArg("v")) {
        g_rotation = constrain((int)g_web.arg("v").toInt(), 0, 3);
        display::setRotation((uint8_t)g_rotation);
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putInt("rot", g_rotation);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

static void handleGps() {   // auto-set the centre point from the LC76G GPS (-G variant)
    if (g_web.hasArg("v")) {
        g_useGps = g_web.arg("v").toInt() != 0;
        if (g_web.hasArg("save")) {
            Preferences p;
            p.begin("capsuleradar", false);
            p.putBool("usegps", g_useGps);
            p.end();
        }
    }
    g_web.send(200, "text/plain", "ok");
}

// ---- browser OTA: upload an app .bin over WiFi and self-flash ----
static void handleUpdatePage() {
    g_web.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Feedback Kiosk - Update</title><style>"
        "body{background:radial-gradient(circle at 50% -10%,#0a1f15,#04100a 70%);color:#cdd6d1;"
        "font-family:system-ui,sans-serif;margin:0 auto;padding:20px;max-width:480px;min-height:100vh}"
        "h1{color:#1dff86;font-size:20px}.card{background:rgba(10,20,14,.85);border:1px solid #1f3a2b;border-radius:14px;padding:16px}"
        "input,button{width:100%;box-sizing:border-box;padding:11px;border-radius:8px;margin-top:8px;font-size:16px}"
        "input{background:#0c1a12;color:#eafff3;border:1px solid #2a4a39}"
        "button{border:0;background:#1dff86;color:#04140b;font-weight:700}"
        "#bar{height:12px;background:#0c1a12;border-radius:6px;overflow:hidden;margin-top:14px;display:none}"
        "#fill{height:100%;width:0;background:#1dff86;transition:width .2s}#msg{margin-top:10px;color:#9affc8;font-size:13px}"
        "a{color:#1dff86}p{color:#9affc8;font-size:13px}"
        "</style></head><body><h1>Firmware update (OTA)</h1><div class=card>"
        "<p>Upload the <b>app firmware</b> <code>CapsuleRadar-ota.bin</code> from the GitHub release. "
        "Do NOT use the merged flash image here.</p>"
        "<input type=file id=f accept='.bin'>"
        "<button onclick=u()>Update over WiFi</button>"
        "<div id=bar><div id=fill></div></div><div id=msg></div></div>"
        "<p style='text-align:center;margin-top:14px'><a href=/>&larr; Back to settings</a></p>"
        "<script>function u(){var f=document.getElementById('f').files[0];if(!f){return}"
        "var x=new XMLHttpRequest(),fd=new FormData();fd.append('f',f);"
        "document.getElementById('bar').style.display='block';"
        "x.upload.onprogress=function(e){if(e.lengthComputable)document.getElementById('fill').style.width=(e.loaded/e.total*100)+'%'};"
        "x.onload=function(){document.getElementById('msg').innerText=x.responseText+' - rebooting...'};"
        "x.onerror=function(){document.getElementById('msg').innerText='Upload failed'};"
        "x.open('POST','/update');x.send(fd);}</script></body></html>");
}

static void handleUpdateUpload() {
    HTTPUpload &up = g_web.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[update] start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[update] done: %u bytes\n", (unsigned)up.totalSize);
        else Update.printError(Serial);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\nCapsule Radar boot");

    if (PIN_LCD_SCLK < 0 || PIN_I2C_SDA < 0) {
        Serial.println("[!] Pins in config.h are still -1. Copy them from the Waveshare demo.");
    }
    Serial.printf("PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());

    loadSettings();
    // --- Event log (load NVS counters; queue the webhook if a URL is set) ---
    feedback_log::begin();
    feedback_log::setWebhookUrl(FEEDBACK_WEBHOOK_URL);    // "" = disabled

    // --- Kiosk operator settings: load from NVS, push into feedback_view +
    //     log (live: question/URLs/cooldown apply now; mode may re-init the view).
    {
        feedback_log::Settings fbset;
        feedback_log::loadSettings(fbset);
        feedback_log::applySettings(fbset);
    }

    // --- Display + LVGL (M0) ----------------------------------------------
    // CO5300 AMOLED over QSPI + LVGL draw buffers in PSRAM, then a hello screen.
    // The panel is powered from the always-on DC1 rail, so it lights without the
    // PMIC. Touch (CST9217 indev) + AXP2101 come in later milestones.
    if (!display::begin()) {
        Serial.println("[!] display::begin() failed — check QSPI pins / power.");
    }

    // restore the saved theme, then persist any future change
    {
        Preferences p;
        p.begin("capsuleradar", true);
        const int t = p.getInt("theme", FEEDBACK_THEME_PHOSPHOR);
        g_showSweep = p.getBool("sweep", true);
        g_showAirports = p.getBool("airports", true);
        g_hideGround = p.getBool("hideground", false);
        g_minAltFt = p.getInt("minalt", 0);
        g_milOnly = p.getBool("milonly", false);
        g_rotation = p.getInt("rot", 0);
        p.end();
        feedback_view::setTheme(t);
        feedback_view::setSweepEnabled(g_showSweep);
        feedback_view::setAirportsEnabled(g_showAirports);
        feedback_view::setTrailLength(g_trailLen);
        feedback_view::setMaxOnScreen(g_maxAc);
        display::setRotation((uint8_t)g_rotation);
    }
    feedback_view::setThemeChangedCb(saveTheme);

    imu_begin();       // face-down sleep (no-op if the IMU isn't detected)
    battery_begin();   // AXP2101 (no-op if not detected / no battery)
    gps_begin();       // LC76G GNSS (no-op if not the -G variant)
    battery_enable_codec_rail();   // power the ES8311 analog rail before audio init

    setenv("TZ", TZ_STR, 1); tzset();   // local time for display even before NTP
    rtc_begin();
    rtc_seed_clock();                   // offline clock/date from the PCF85063
    if (audio_begin()) {                // ES8311 alert pings (no-op if codec absent)
        audio_set_volume(g_volume);
        audio_set_muted(g_muted);
    }

    // --- Radar UI ----------------------------------------------------------
    // radar::init() runs inside display::begin() (LVGL must be up first).

    // --- WiFi (captive portal, non-blocking) ------------------------------
    // First boot opens the "CapsuleRadar-Setup" AP to enter WiFi creds. Non-blocking
    // so the radar keeps animating while you configure WiFi from your phone.
    g_wm.setConfigPortalBlocking(false);
    g_wm.setTitle("Feedback Kiosk");
    // light phosphor-green theme for the captive portal (small CSS, injected into <head>)
    g_wm.setCustomHeadElement(
        "<style>"
        "body{background:#06100a;color:#cdd6d1;font-family:system-ui,sans-serif}"
        "h1,h2,h3{color:#1dff86}"
        "button,input[type=submit],.btn{background:#1dff86!important;color:#04140b!important;"
        "border:0!important;border-radius:8px!important;font-weight:700}"
        "input,select{background:#0c1a12!important;color:#eafff3!important;"
        "border:1px solid #2a4a39!important;border-radius:8px!important}"
        "a{color:#1dff86}.q{filter:hue-rotate(90deg)}"
        "</style>");
    // After the portal saves new credentials, reboot for a clean start: WiFiManager's
    // own port-80 server (and mDNS) don't cleanly hand over to our web server / STA
    // interface in non-blocking mode, so the config page is flaky until a fresh boot.
    g_wm.setSaveConfigCallback([]() {
        Serial.println("[wifi] new credentials saved -> rebooting for a clean web/mDNS start");
        g_rebootAtMs = millis() + 2500;   // let the portal deliver its 'saved' page first
    });
    if (g_wm.autoConnect("Feedback-Setup"))
        Serial.println("[wifi] connected");
    else
        Serial.println("[wifi] config portal open - join 'Feedback-Setup' to set WiFi; UI stays live");

    // --- OTA ---------------------------------------------------------------
    // ArduinoOTA is started from loop() once WiFi connects (see otaUp there).

    // --- (no application task; the feedback terminal is fully on-device) ---

    // configuration web page (http://feedback.local/) + /stats dashboard + API
    g_web.on("/", handleRoot);
    g_web.on("/save", HTTP_POST, handleSave);
    g_web.on("/stats", handleStatsPage);
    g_web.on("/api/stats", handleStatsApi);
    g_web.on("/webhook", handleWebhookUrl);
    g_web.on("/mode", handleMode);
    g_web.on("/q", handleQuestion);
    g_web.on("/urlrev", handleUrl);
    g_web.on("/urlin", handleUrl);
    g_web.on("/cooldown", handleCooldown);
    g_web.on("/wifi", HTTP_POST, handleWifi);
    g_web.on("/bright", handleBright);
    g_web.on("/vol", handleVol);
    g_web.on("/alerts", handleAlerts);
    g_web.on("/idle", handleIdle);
    g_web.on("/sweep", handleSweep);
    g_web.on("/airports", handleAirports);
    g_web.on("/ground", handleGround);
    g_web.on("/altmin", handleAltMin);
    g_web.on("/milonly", handleMilOnly);
    g_web.on("/trail", handleTrail);
    g_web.on("/maxac", handleMaxAc);
    g_web.on("/rotate", handleRotate);
    g_web.on("/gps", handleGps);
    g_web.on("/units", handleUnits);
    g_web.on("/update", HTTP_GET, handleUpdatePage);
    g_web.on("/update", HTTP_POST,
        []() {
            const bool ok = !Update.hasError();
            g_web.send(200, "text/plain", ok ? "OK" : "FAIL");
            delay(800);
            if (ok) ESP.restart();
        },
        handleUpdateUpload);
    g_web.begin();

    Serial.println("setup done");
}

void loop() {
    display::loop();                // drive LVGL (render dirty areas + run timers)
    g_wm.process();                 // service the WiFi config portal (non-blocking)
    g_web.handleClient();           // serve the configuration web page

    feedback_log::loop();         // throttled NVS flush + webhook queue drain (no-op when idle)
    // scheduled reboot after a fresh WiFi config (see setSaveConfigCallback)
    if (g_rebootAtMs && (int32_t)(millis() - g_rebootAtMs) >= 0) { delay(50); ESP.restart(); }

    // OTA: set up once WiFi is up, then service it every loop (flash over the air)
    static bool otaUp = false;
    if (!otaUp && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname("feedback");            // -> feedback.local (registers mDNS)
        ArduinoOTA.begin();
        MDNS.addService("http", "tcp", 80);            // advertise the config web page
        otaUp = true;
        Serial.println("[ota] ready: pio run -e esp32-s3-amoled-175-ota -t upload");
    }
    if (otaUp) ArduinoOTA.handle();


    // periodic: HUD clock + wifi/battery indicators
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 5000) {
        lastStatus = millis();
#if DEBUG_MEM
        static uint32_t lastFrames = 0;
        const uint32_t fr = display_frames();
        const unsigned fps = (fr - lastFrames) / 5;
        lastFrames = fr;
        Serial.printf("[mem] heap %u (min %u, biggest %u) | psram %u free | up %lus | aircraft %d | fps %u\n",
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)ESP.getFreePsram(), (unsigned long)(millis() / 1000),
                      (int)g_snap.size(), fps);
#endif
        char clk[8] = "--:--";
        struct tm ti;
        const bool haveTime = getLocalTime(&ti, 0);
        if (haveTime) {
            snprintf(clk, sizeof(clk), "%02d:%02d", ti.tm_hour, ti.tm_min);
            char date[20];
            strftime(date, sizeof(date), "%d %b %Y", &ti);   // e.g. "08 Jun 2026"
            ui_set_date(date);
        }
        const bool wifiUp = (WiFi.status() == WL_CONNECTED);
        const int  rssi   = wifiUp ? (int)WiFi.RSSI() : -127;
        ui_set_status(wifiUp, wifiUp, rssi, clk);
        char net[80];
        if (WiFi.status() == WL_CONNECTED)
            snprintf(net, sizeof(net), "Configure at\nfeedback.local\n%s", WiFi.localIP().toString().c_str());
        else
            snprintf(net, sizeof(net), "WiFi setup:\njoin Feedback-Setup");
        ui_set_netinfo(net);
        const bool bpresent = battery_present();
        ui_set_battery(battery_percent(), battery_charging(), bpresent);
        g_onBattery = bpresent && !battery_charging();
        // once NTP has a real fix, persist it to the RTC (core 1 only)
        if (!g_rtcSynced && time(nullptr) > 1700000000L) {
            time_t now = time(nullptr);
            struct tm utc;
            gmtime_r(&now, &utc);
            if (rtc_write(&utc)) { g_rtcSynced = true; Serial.println("[rtc] saved NTP time"); }
        }
    }

    // face-down -> screen off (IMU); flip face-up to wake
    static uint32_t lastImu = 0;
    static int fdCount = 0;
    if (millis() - lastImu > 400) {
        lastImu = millis();
        const int fd = imu_facedown();              // 1 down, 0 not, -1 read error
        if (fd > 0)       { if (fdCount < 8) fdCount++; }
        else if (fd == 0) fdCount = 0;              // -1 (I2C hiccup): leave the counter as-is
        const bool sleep = (fdCount >= 4);   // ~1.6 s face-down
        const bool idle  = g_idleDimMs > 0 && display::inactiveMs() > g_idleDimMs;
        if (sleep != g_asleep || idle != g_idle) {
            g_asleep = sleep;
            g_idle = idle;
            applyBrightness();
        }
    }

    delay(5);
}
