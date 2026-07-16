# Feedback Kiosk 🙂

<p align="center">
  <a href="https://socquique.github.io/capsule-radar/"><img src="https://img.shields.io/badge/Flash%20in%20browser-FF6D00?logo=googlechrome&logoColor=white" alt="Flash in browser"></a>
  <img src="https://img.shields.io/badge/board-ESP32--S3%20round%20AMOLED-E7352C?logo=espressif&logoColor=white" alt="Board: ESP32-S3 round AMOLED">
  <a href="https://github.com/socquique/capsule-radar/releases"><img src="https://img.shields.io/github/v/tag/socquique/capsule-radar?label=firmware&color=7B42BC" alt="Firmware version"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/code-MIT-2088FF" alt="License: MIT"></a>
</p>

An **anonymous customer-feedback terminal** for the **Waveshare ESP32-S3-Touch-AMOLED-1.75** (round 466×466 AMOLED, capacitive touch). Tap a smiley, see a thank-you pulse, and get a QR for the public review page. Every interaction is logged locally and (if configured) POSTed to a webhook — **nothing personal is ever stored or transmitted**: only `(timestamp, answer)`.

Designed for **unattended kiosk use**: watchdog-protected, WiFi self-healing, configurable on-screen question + dual-mode flow.

## Two modes

- **Review** (kapper / hair-salon): tap a smiley → THANK YOU pulse → QR code for the public review page (Google Maps, etc.). Bad / neutral taps show the *internal* feedback form QR first and a smaller *public* review QR second, so no customer is forced to leave a public review.
- **Dashboard** (wachtkamer / waiting room): tap a smiley → THANK YOU pulse → straight back to IDLE. No QR is ever shown. All answers still go to the dashboard (NVS + webhook) — only the on-screen QR follow-up is suppressed.

Mode is set in the web config and stored in NVS; live update on save.

## Flow

```
IDLE (question + 3 smileys)        THANK YOU (~1.5 s)        QR (Review only, 12 s)
   tap GOOD                        "Bedankt voor uw             big QR: public review
   tap NEUTRAL                      feedback!"                  small QR: internal form (only on BAD/NEUTRAL)
   tap BAD                                                   auto-returns to IDLE (or tap)
        \                          soft ping via ES8311
         \______________________________________________
                                  COOLDOWN arc (~4 s) absorbs rapid taps
                                  so a single user can't pollute the stats
```

## Anonymity guarantee

Privacy is **by design** (GDPR / AVG):

- The only event data written or sent is `{timestamp, answer}` — no IDs, no device IDs, no network metadata.
- `answer` is an integer (0 = good, 1 = neutral, 2 = bad).
- `timestamp` is the unix epoch; never combined with anything identifying.
- A "no personal data" CSV row, a "no PII" NVS blob, a "no payload PII" webhook JSON.
- The configuration web page runs only on the device's own WiFi (mDNS at `http://feedback.local/`); it is not exposed to the internet.

## Local logging

Every event is captured in three places at once (any of which can fail silently):

| Surface          | Where                                 | When                       |
|------------------|---------------------------------------|----------------------------|
| RAM ring buffer  | last 500 events                       | instant (read by `/api/stats`) |
| Daily counters   | NVS namespace `feedback_log`, 31 days | throttled (max once per 30 s) |
| CSV              | `/sdcard/feedback.csv` (device) or `./feedback.csv` (sim) | instant |
| Webhook (opt.)   | configurable URL                       | fire-and-forget from a low-priority FreeRTOS task, 3 s timeout, single shot, drops offline |

## Configuration

Browse to `http://feedback.local/` on the same WiFi to set:

- **Mode** (Review / Dashboard)
- **Question text** (max 60 chars)
- **Public review URL** (Google Maps review link)
- **Internal feedback form URL**
- **Webhook URL** (optional JSON endpoint)
- **Cooldown** between interactions (2–30 s, default 4)
- Brightness + WiFi reset (live)

Saving the question, URLs, webhook and cooldown takes effect immediately; mode change may restart the view.

The `/stats` dashboard (also on the LAN) shows today's counts, the last 31 days, and a weekday × daypart heatmap built from the 500-event ring buffer. JSON snapshot at `/api/stats`.

On-device admin: hold for **3 seconds anywhere on the IDLE screen** to open a small read-only overlay with today's counts, the IP address / config URL, the firmware version, and a close button. The overlay auto-dismisses after 10 s. No settings can be changed on-device — use the web page.

## Build & flash

```bash
pio run -e esp32-s3-amoled-175 -t upload     # build + flash over USB-C
pio run -e native -t exec                    # desktop LVGL simulator (SDL2)
```

On first flash, hold **BOOT** then tap **RESET**; on first boot connect to the `Feedback-Setup` WiFi to set your network.

The hardware watchdog (`esp_task_wdt`) resets the device if the UI loop ever locks up, and the WiFi reconnect path tries every 30 s when offline — so the kiosk can run unattended for weeks.

For browser-based flashing, the project ships a web flasher (ESP Web Tools) at the GitHub Pages URL above. The `.github/workflows/webflasher.yml` builds and publishes it automatically; tagged releases attach a ready-to-flash `feedback-kiosk-esp32s3.bin` via `release.yml`.

## Hardware

Waveshare **ESP32-S3-Touch-AMOLED-1.75**: ESP32-S3R8 (8 MB PSRAM, 16 MB flash), **CO5300** AMOLED over QSPI, **CST9217** touch, **QMI8658** IMU, **PCF85063** RTC, **AXP2101** PMIC, **ES8311** audio + speaker. All pins are in `src/config.h` (sourced from the board definition; no guessing).

## Repo layout

```
src/
  config.h              pins + tunables
  main.cpp              setup/loop: WiFi + NTP + web config + watchdog
  display.*             CO5300 (Arduino_GFX) + LVGL bring-up
  feedback_view.*       kiosk state machine (IDLE / THANKS / QR / COOLDOWN / admin)
  feedback_log.*        anonymous event log (ring + NVS + CSV + webhook)
  ui.*                  top HUD + boot splash
  touch_cst9217.*       capacitive touch driver
  imu_qmi8658.*         accelerometer (face-down sleep)
  battery.*             AXP2101 battery gauge
  rtc_pcf85063.*        PCF85063 real-time clock
  audio.*               ES8311 soft ping
  sim_main.cpp          native SDL simulator (not flashed)
include/lv_conf.h       LVGL config (v8)
web/flash/              browser web-flasher (ESP Web Tools)
scripts/                build_webflasher.sh
docs/                   hardware / data-source / architecture notes
```

## Data & license

**Firmware / code: [MIT](LICENSE)** — fork and build on it freely. The firmware deliberately stores and transmits only an anonymous answer and a timestamp; no personal data, no device IDs, no network metadata. Personal / hobby project.
