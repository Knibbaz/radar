// Native (Mac/Linux) LVGL simulator for the Feedback Kiosk.
// Runs the SAME LVGL UI (include/lv_conf.h + ui_create + feedback_view) in an
// SDL2 window — no hardware, no Arduino_GFX. Only compiled for the `native`
// PlatformIO env.
//
//   pio run -e native            # build
//   pio run -e native -t exec    # build + run (or run .pio/build/native/program)
//
// Note: the real panel is a 466x466 *round* AMOLED; this square window shows the
// full buffer, so the corners (hidden on the device) are visible here.
#include <SDL.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "feedback_view.h"
#include "ui.h"

#define SIM_W SCREEN_W   // 466
#define SIM_H SCREEN_H   // 466

static SDL_Window   *s_win = NULL;
static SDL_Renderer *s_ren = NULL;
static SDL_Texture  *s_tex = NULL;

// LVGL -> SDL texture. Push the dirty area, then repaint the whole window.
static void sdl_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(s_tex, &r, px, w * (int)sizeof(lv_color_t));
    if (lv_disp_flush_is_last(drv)) {
        SDL_RenderClear(s_ren);
        SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
        SDL_RenderPresent(s_ren);
    }
    lv_disp_flush_ready(drv);
}

// Mouse acts as the touch input device (left button = press).
static void sdl_mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int x, y;
    Uint32 btn = SDL_GetMouseState(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = (btn & SDL_BUTTON(SDL_BUTTON_LEFT)) ? LV_INDEV_STATE_PRESSED
                                                      : LV_INDEV_STATE_RELEASED;
}

// Simple HUD ticker — drives the clock/date/battery labels the real device
// would push from NTP/PMIC. The real firmware (ui.cpp's loop) calls these; we
// do the same so the sim screen looks alive.
static void sim_tick_hud(Uint32 now) {
    static Uint32 last = 0;
    if (now - last < 1000) return;
    last = now;

    const time_t t = time(nullptr);
    struct tm lt;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char clk[8];
    snprintf(clk, sizeof(clk), "%02d:%02d", lt.tm_hour, lt.tm_min);
    char date[20];
    strftime(date, sizeof(date), "%d %b %Y", &lt);

    ui_set_status(true, true, -58, clk);          // WiFi strong, "feed" healthy
    ui_set_battery(78, false, true);              // mock battery
    ui_set_date(date);                            // local date
    ui_set_netinfo("Configure at\nfeedback.local\n192.168.1.42");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    setvbuf(stdout, NULL, _IOLBF, 0);  // line-buffered: logs appear even when piped to a file

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("[sim] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    s_win = SDL_CreateWindow("Feedback Kiosk (sim)",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SIM_W, SIM_H, SDL_WINDOW_ALLOW_HIGHDPI);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_win || !s_ren) {
        printf("[sim] window/renderer creation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(s_ren, SIM_W, SIM_H);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, SIM_W, SIM_H);
    printf("[sim] SDL video driver: %s\n", SDL_GetCurrentVideoDriver());

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[SIM_W * 100];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SIM_W * 100);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = sdl_flush;
    disp_drv.hor_res  = SIM_W;
    disp_drv.ver_res  = SIM_H;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    ui_create();    // builds feedback_view::init(scr) + the top HUD; auto-splash fades
    printf("[sim] Feedback Kiosk simulator running (%dx%d). Click the smileys.\n", SIM_W, SIM_H);

    Uint32 last = SDL_GetTicks();
    bool run = true;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
        }
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last);
        last = now;
        sim_tick_hud(now);
        lv_timer_handler();
        SDL_Delay(5);
    }

    SDL_DestroyTexture(s_tex);
    SDL_DestroyRenderer(s_ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
