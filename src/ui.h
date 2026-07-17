#pragma once
// Kiosk UI: top HUD (WiFi bars / clock / battery / date) + the boot splash.
// Pure LVGL, portable (device + SDL simulator). The full-screen content is
// owned by feedback_view (built as a child of the active screen in ui_create).
void ui_create(void);            // build the HUD + the feedback view on the active screen
void ui_on_data_updated(void);   // reserved (radar-era; no-op in kiosk)
void ui_show_view(int idx);      // reserved (radar-era; no-op in kiosk)
void ui_set_status(bool wifiUp, bool feedOk, int rssi, const char *clock);  // HUD: signal bars + clock
void ui_set_battery(int pct, bool charging, bool present);  // top HUD battery indicator
void ui_set_date(const char *date);  // top HUD date line (e.g. "08 Jun 2026")
void ui_set_netinfo(const char *line);  // reserved (was Stats view footer)
void ui_set_gps(int state, int sats);   // reserved (radar-era; no-op in kiosk)
void ui_splash_show(void);  // branded boot splash (auto-fades, covers init time)
void ui_set_range_cb(void (*cb)(float km));  // reserved (was zoom button; no-op in kiosk)
void ui_set_range_km(float km);              // reserved (radar-era; no-op in kiosk)
void ui_set_units(int preset);               // reserved (radar-era; no-op in kiosk)
void ui_set_hud_visible(bool visible);       // show/hide the top HUD for CHOICE/POP screens
