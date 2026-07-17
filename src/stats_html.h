// JSON renderer for the /api/stats endpoint and the %STATS_JSON% placeholder
// in the dashboard page. Pulls from feedback_log (today + 31-day series +
// weekday x daypart heat grid).
//
// The HTML page itself lives in web_pages.h so the device firmware and the
// native browser preview render the same UI from one source of truth.

#pragma once
#include <stdio.h>
#include <string.h>
#include "feedback_log.h"

namespace stats_html {

// Render the full stats JSON into `out`. Returns bytes written (excl. NUL),
// or -1 on overflow. The shape is:
//   {"today":{"good":N,"neutral":N,"bad":N,"happy_pct":N,"total":N},
//    "days":[{"y":Y,"m":M,"d":D,"good":N,"neutral":N,"bad":N}, ...],
//    "heat":[[{"good":N,"total":N},{"good":N,"total":N},{"good":N,"total":N}], ...]}
static int render_json(char *out, size_t cap) {
    if (!out || cap == 0) return 0;

    const feedback_log::DayCount today = feedback_log::day(0);
    const uint32_t td_total = today.good + today.neutral + today.bad;
    const int td_happy = (td_total > 0) ? (int)((today.good * 100) / td_total) : 0;

    feedback_log::HeatGrid heat;
    feedback_log::buildHeatGrid(heat);

    feedback_log::DaySeries ds;
    feedback_log::build31DaySeries(ds);

    int n = snprintf(out, cap,
        "{\"today\":{\"good\":%u,\"neutral\":%u,\"bad\":%u,\"happy_pct\":%d,\"total\":%u},"
        "\"days\":[",
        (unsigned)today.good, (unsigned)today.neutral, (unsigned)today.bad,
        td_happy, (unsigned)td_total);
    if (n < 0 || (size_t)n >= cap) return -1;

    for (int i = 0; i < ds.count; ++i) {
        const int w = snprintf(out + n, cap - n,
            "%s{\"y\":%u,\"m\":%u,\"d\":%u,\"good\":%u,\"neutral\":%u,\"bad\":%u}",
            (i == 0 ? "" : ","),
            (unsigned)ds.year[i], (unsigned)ds.mon[i], (unsigned)ds.day[i],
            (unsigned)ds.good[i], (unsigned)ds.neutral[i], (unsigned)ds.bad[i]);
        if (w < 0 || (size_t)w >= cap - n) return -1;
        n += w;
    }
    const int w1 = snprintf(out + n, cap - n, "],\"heat\":[");
    if (w1 < 0 || (size_t)w1 >= cap - n) return -1;
    n += w1;

    for (int wd = 0; wd < 7; ++wd) {
        const int w2 = snprintf(out + n, cap - n, "%s[", (wd == 0 ? "" : ","));
        if (w2 < 0 || (size_t)w2 >= cap - n) return -1;
        n += w2;
        for (int dp = 0; dp < 3; ++dp) {
            const int w3 = snprintf(out + n, cap - n,
                "%s{\"good\":%u,\"total\":%u}",
                (dp == 0 ? "" : ","),
                (unsigned)heat.cell[wd][dp].good,
                (unsigned)heat.cell[wd][dp].total);
            if (w3 < 0 || (size_t)w3 >= cap - n) return -1;
            n += w3;
        }
        const int w4 = snprintf(out + n, cap - n, "]");
        if (w4 < 0 || (size_t)w4 >= cap - n) return -1;
        n += w4;
    }
    const int w5 = snprintf(out + n, cap - n, "]}");
    if (w5 < 0 || (size_t)w5 >= cap - n) return -1;
    n += w5;
    return n;
}

} // namespace stats_html
