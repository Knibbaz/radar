// Stats/dashboard rendering for the kiosk.
//
//   /stats       -> self-contained HTML (no external assets), auto-refresh 60 s
//   /api/stats   -> JSON snapshot of the same data
//
// Both are pure functions: they take an output buffer and a length, and
// return the number of bytes written (or 0 on overflow). The handlers in
// main.cpp allocate a stack buffer and pass it in.
//
// Bounded heap usage: callers pass `static char buf[N]`, we don't alloc.

#pragma once
#include "feedback_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace stats_html {

// HTML escape: enough for the dashboard (RFC 3986 + '"' + '<' + '>').
static int html_escape(char *out, size_t cap, const char *s) {
    int n = 0;
    if (!s) return 0;
    while (*s && n + 8 < (int)cap) {
        unsigned char c = (unsigned char)*s++;
        if      (c == '<') { n += snprintf(out + n, cap - n, "&lt;");  }
        else if (c == '>') { n += snprintf(out + n, cap - n, "&gt;");  }
        else if (c == '&') { n += snprintf(out + n, cap - n, "&amp;"); }
        else if (c == '"') { n += snprintf(out + n, cap - n, "&quot;");}
        else if (c < 0x20) { n += snprintf(out + n, cap - n, "&#%d;", c); }
        else               { out[n++] = (char)c; }
    }
    out[n] = 0;
    return n;
}

// Pick a colour from a "happy %" score: green >= 75, amber >= 50, red < 50.
// We deliberately avoid an external stylesheet, so the CSS lives inline.
static inline const char *heat_color(int good, int total) {
    if (total == 0)     return "#1a2a22";
    const int pct = (good * 100) / total;
    if (pct >= 75) return "#1dff86";
    if (pct >= 50) return "#cdd66c";
    if (pct >= 25) return "#ffb23c";
    return "#ff5a3c";
}

// Daypart label
static inline const char *daypart_name(int dp) {
    return dp == 0 ? "ochtend" : dp == 1 ? "middag" : "avond";
}

// Render the operator dashboard. Caller-supplied buffer; returns bytes used.
static int render_html(char *out, size_t cap) {
    if (!out || cap == 0) return 0;

    const feedback_log::DayCount today = feedback_log::day(0);
    const uint32_t td_total = today.good + today.neutral + today.bad;
    const int td_happy = (td_total > 0) ? (int)((today.good * 100) / td_total) : 0;

    feedback_log::HeatGrid heat;
    feedback_log::buildHeatGrid(heat);

    feedback_log::DaySeries ds;
    feedback_log::build31DaySeries(ds);

    // Start with the HTML head + style. Small, no external assets, dark UI.
    int n = snprintf(out, cap,
        "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta http-equiv=refresh content='60'>"
        "<title>Feedback Kiosk - Dashboard</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{background:radial-gradient(circle at 50%% -10%%,#0a1f15,#04100a 70%%);color:#cdd6d1;"
        "font-family:system-ui,-apple-system,sans-serif;margin:0 auto;padding:20px;max-width:780px}"
        "h1{color:#1dff86;font-size:22px;margin:0 0 4px}"
        ".sub{color:#6f8c7d;font-size:13px;margin:0 0 18px}"
        ".card{background:rgba(10,20,14,.85);border:1px solid #1f3a2b;border-radius:14px;padding:16px;margin-bottom:14px}"
        ".t{color:#1dff86;font-size:11px;letter-spacing:1.5px;text-transform:uppercase;margin-bottom:10px;opacity:.85}"
        ".nums{display:flex;gap:14px;flex-wrap:wrap}"
        ".big{flex:1;min-width:140px;padding:14px;border-radius:12px;background:#0c1a12;border:1px solid #2a4a39;text-align:center}"
        ".big .v{font-size:30px;font-weight:700;color:#eafff3;margin:6px 0 2px}"
        ".big .l{color:#9affc8;font-size:11px;letter-spacing:1px;text-transform:uppercase}"
        ".big.g .v{color:#1dff86}.big.n .v{color:#ffb23c}.big.b .v{color:#ff5a3c}"
        ".bar{height:14px;border-radius:7px;background:#0c1a12;border:1px solid #2a4a39;overflow:hidden}"
        ".fill{height:100%%;background:linear-gradient(90deg,#1dff86,#9affc8)}"
        "table{border-collapse:collapse;width:100%%;font-size:13px}"
        "th,td{padding:6px 8px;text-align:center;border-bottom:1px solid #143020}"
        "th{color:#9affc8;font-weight:600}"
        ".heat td{height:34px;width:90px;color:#000;font-weight:600}"
        "td.empty{background:#0c1a12 !important;color:#5f7a6c !important}"
        ".ft{color:#5f7a6c;font-size:12px;text-align:center;margin-top:14px}"
        ".ft code{color:#9affc8}"
        "</style></head><body>"
        "<h1>Feedback Kiosk &mdash; Dashboard</h1>"
        "<p class=sub>Laatste 31 dagen &middot; auto-refresh elke 60s</p>");

    if (n < 0 || (size_t)n >= cap) return 0;

    // --- TODAY ---
    n += snprintf(out + n, cap - n,
        "<div class=card><div class=t>Vandaag</div><div class=nums>"
        "<div class='big g'><div class=l>GOED</div><div class=v>%u</div></div>"
        "<div class='big n'><div class=l>NEUTRAAL</div><div class=v>%u</div></div>"
        "<div class='big b'><div class=l>ONTEVREDEN</div><div class=v>%u</div></div>"
        "</div>"
        "<div style='margin-top:14px'><div class=bar><div class=fill style='width:%d%%'></div></div>"
        "<div style='margin-top:6px;color:#9affc8;font-size:12px'>Tevreden: <b style='color:#1dff86'>%d%%</b> &middot; %u reacties</div></div>"
        "</div>",
        (unsigned)today.good, (unsigned)today.neutral, (unsigned)today.bad,
        td_happy, td_happy, (unsigned)td_total);
    if ((size_t)n >= cap) return 0;

    // --- 31-DAY BARS ---
    n += snprintf(out + n, cap - n, "<div class=card><div class=t>Laatste 31 dagen</div><table>");
    if ((size_t)n >= cap) return 0;
    for (int i = 0; i < ds.count; ++i) {
        const uint32_t tot = ds.good[i] + ds.neutral[i] + ds.bad[i];
        const int w = (tot > 0) ? (int)((ds.good[i] * 100) / tot) : 0;
        n += snprintf(out + n, cap - n,
            "<tr><td style='text-align:left;color:#9affc8'>%02u-%02u-%u</td>"
            "<td style='text-align:right;color:#1dff86'>%u</td>"
            "<td style='text-align:right;color:#cdd66c'>%u</td>"
            "<td style='text-align:right;color:#ff5a3c'>%u</td>"
            "<td style='width:50%%'><div class=bar><div class=fill style='width:%d%%'></div></div></td></tr>",
            ds.mon[i], ds.day[i], (unsigned)ds.year[i],
            (unsigned)ds.good[i], (unsigned)ds.neutral[i], (unsigned)ds.bad[i], w);
        if ((size_t)n >= cap) return 0;
    }
    n += snprintf(out + n, cap - n, "</table></div>");
    if ((size_t)n >= cap) return 0;

    // --- HEAT TABLE (weekday x daypart) ---
    static const char *WD[7] = {"Zo", "Ma", "Di", "Wo", "Do", "Vr", "Za"};
    n += snprintf(out + n, cap - n,
        "<div class=card><div class=t>Patronen &mdash; per dagdeel</div>"
        "<p class=sub style='margin:0 0 10px'>Kleur = %% tevreden. Lege cel = geen data.</p>"
        "<table class=heat><tr><th></th><th>ochtend</th><th>middag</th><th>avond</th></tr>");
    if ((size_t)n >= cap) return 0;
    for (int wd = 0; wd < 7; ++wd) {
        n += snprintf(out + n, cap - n, "<tr><th>%s</th>", WD[wd]);
        for (int dp = 0; dp < 3; ++dp) {
            const uint32_t tot = heat.cell[wd][dp].total;
            const uint32_t gd  = heat.cell[wd][dp].good;
            const char *cls = (tot == 0) ? "empty" : "";
            const char *bg  = heat_color((int)gd, (int)tot);
            const char *lbl = (tot == 0) ? "&mdash;" :
                              (snprintf((char*)out + n, 1, "%s", ""), "");
            (void)lbl;
            n += snprintf(out + n, cap - n,
                "<td style='background:%s' class='%s'>%s</td>",
                bg, cls, tot == 0 ? "&mdash;" : "");
            // content: render the count + percent inline so the cell is the info source itself
            if (tot > 0) {
                const int pct = (gd * 100) / tot;
                n += snprintf(out + n, cap - n, "%d%% (%u)", pct, (unsigned)gd);
            }
            n += snprintf(out + n, cap - n, "</td>");
            if ((size_t)n >= cap) return 0;
        }
        n += snprintf(out + n, cap - n, "</tr>");
        if ((size_t)n >= cap) return 0;
    }
    n += snprintf(out + n, cap - n, "</table></div>");
    if ((size_t)n >= cap) return 0;

    // --- FOOTER ---
    n += snprintf(out + n, cap - n,
        "<p class=ft>Endpoint: <code>/api/stats</code> &middot; settings: <a href=/ style='color:#9affc8'>/</a></p>"
        "</body></html>");
    if ((size_t)n >= cap) return 0;
    return n;
}

// JSON snapshot (same data, external tools can ingest).
static int render_json(char *out, size_t cap) {
    if (!out || cap == 0) return 0;

    const feedback_log::DayCount today = feedback_log::day(0);
    const uint32_t td_total = today.good + today.neutral + today.bad;
    const int td_happy = (td_total > 0) ? (int)((today.good * 100) / td_total) : 0;

    feedback_log::HeatGrid heat;
    feedback_log::buildHeatGrid(heat);

    feedback_log::DaySeries ds;
    feedback_log::build31DaySeries(ds);

    int n = snprintf(out, cap, "{\"today\":{\"good\":%u,\"neutral\":%u,\"bad\":%u,\"happy_pct\":%d,\"total\":%u},\"days\":[",
                     (unsigned)today.good, (unsigned)today.neutral, (unsigned)today.bad,
                     td_happy, (unsigned)td_total);
    if ((size_t)n >= cap) return 0;
    for (int i = 0; i < ds.count; ++i) {
        n += snprintf(out + n, cap - n, "%s{\"date\":\"%04u-%02u-%02u\",\"good\":%u,\"neutral\":%u,\"bad\":%u}",
                      i ? "," : "",
                      (unsigned)ds.year[i], (unsigned)ds.mon[i], (unsigned)ds.day[i],
                      (unsigned)ds.good[i], (unsigned)ds.neutral[i], (unsigned)ds.bad[i]);
        if ((size_t)n >= cap) return 0;
    }
    n += snprintf(out + n, cap - n, "],\"heat\":[");
    if ((size_t)n >= cap) return 0;
    static const char *WD[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    for (int wd = 0; wd < 7; ++wd) {
        n += snprintf(out + n, cap - n, "%s\"%s\":[", wd ? "," : "", WD[wd]);
        for (int dp = 0; dp < 3; ++dp) {
            const uint32_t tot = heat.cell[wd][dp].total;
            const uint32_t gd  = heat.cell[wd][dp].good;
            const int pct = (tot > 0) ? (int)((gd * 100) / tot) : 0;
            n += snprintf(out + n, cap - n, "%s{\"part\":\"%s\",\"good\":%u,\"total\":%u,\"pct\":%d}",
                          dp ? "," : "", daypart_name(dp), (unsigned)gd, (unsigned)tot, pct);
            if ((size_t)n >= cap) return 0;
        }
        n += snprintf(out + n, cap - n, "]");
        if ((size_t)n >= cap) return 0;
    }
    n += snprintf(out + n, cap - n, "]}");
    if ((size_t)n >= cap) return 0;
    return n;
}

} // namespace stats_html
