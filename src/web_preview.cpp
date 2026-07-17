// Browser preview generator for the feedback kiosk dashboard.
// Writes HTML preview files to disk when the simulator starts.
// Native-only — not compiled on device.
#include <stdio.h>
#include <string.h>
#include "web_pages.h"

static void write_preview(const char *path, const char *html, size_t len) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[preview] failed to write %s\n", path); return; }
    fwrite(html, 1, len, f);
    fclose(f);
    printf("[preview] wrote %s (%zu bytes)\n", path, len);
}

void web_preview_generate(void) {
    // Generate stats dashboard preview with mock data
    char buf[8192];
    int n = web_pages::render_stats_html(buf, sizeof(buf));
    if (n > 0) {
        write_preview("web/preview/stats.html", buf, (size_t)n);
    }
}