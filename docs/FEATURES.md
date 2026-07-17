# Features — Plane Radar 2.0

Visual target: `assets/plane_radar_2.0_mockup.html`.

## Look (the "prettier")
- True-black AMOLED background; phosphor-green scope, glow on the sweep edge.
- Concentric range rings + crosshair + N/E/S/W rose; outer-ring range label (e.g. "15 km").
- Rotating sweep with a trailing alpha gradient.
- Aircraft as **plane glyphs rotated by track/heading** (not dots).
- **Altitude color map**: ≤3k ft red → 3–10k amber → 10–20k lime → 20–30k green → 30k+ cyan.
- **Fading trail** behind each aircraft (last N positions / direction tail).
- Center "you" dot with a soft pulse.
- Top HUD: WiFi strength, aircraft count, clock.

## Functions (the "more")
1. **Touch to inspect** — tap nearest glyph → detail card: callsign, type, registration (if available), altitude, ground speed, vertical-rate arrow, distance, bearing, squawk.
2. **Views** (swipe): Radar · List (sorted by distance) · Detail · Stats (count, closest, max alt, msgs).
3. **Range zoom** — cycle 5 / 10 / 25 / 100 km (tap or long-press), re-query radius accordingly.
4. **Orientation** — north-up ↔ track-up toggle.
5. **Alerts** — highlight + speaker **ping** for: emergency squawks (7500/7600/7700), military (`dbFlags`), or a user watch-list of types (A380, B52…). Card flashes red (see RESCUE51 in the mockup).
6. **Night auto-dim** — use PCF85063 RTC to lower brightness after dusk.
7. **IMU gestures** (QMI8658) — face-down → screen sleep; face-up → wake; shake → force refresh.
8. **Setup & maintenance** — first-boot **captive portal** (WiFi creds + home lat/lon + range); settings in NVS; **OTA** updates.

## Nice-to-have / later
- Route enrichment (origin→destination) via a secondary lookup.
- Sound themes / mute.
- Multiple saved home locations.
- microSD logging of seen aircraft.

## MakerWorld packaging
- Parametric printable enclosure for the round board (bezel + stand), à la the original radar.
- Publish firmware + STLs; include a looping GIF of the live sweep. The original radar earned MakerWorld "featured/boost" badges — same formula here for points toward the P2S.

---

# Feedback Kiosk — Status

New: the firmware was pivoted from plane radar to a customer-feedback terminal ("smiley kiosk")
for the same Waveshare ESP32-S3-Touch-AMOLED-1.75 board. See `README.md` for the device manual.

## Fase 1 — Boot & display (done)
- [x] CO5300 AMOLED bring-up (QSPI)
- [x] LVGL v8.4 init with touch (CST9217)
- [x] AXP2101 battery gauge, PCF85063 RTC, ES8311 audio ping

## Fase 2 — Core flow (done)
- [x] State machine: LOGO → CHOICE → POP → QR → LOGO
- [x] LOGO: dim idle, centered placeholder/logo, drift burn-in (±8px/30s)
- [x] CHOICE: green/red split, 2 smileys, 10s timeout
- [x] POP: expanding circle animation (400ms ease-out)
- [x] QR: white 300x300 tile on black, tap/12s → LOGO
- [x] Client-side logo resize + SPIFFS upload + remove
- [x] Admin overlay (long-press: counts, IP, config URL)

## Fase 3 — Logging (done)
- [x] RAM ring buffer (500 events), 31-day NVS rolling aggregates
- [x] CSV append (SD card), optional webhook (fire-and-forget)

## Fase 4 — Dashboard & API (done)
- [x] /stats HTML + /api/stats JSON, heat grid (weekday × daypart)

## Fase 5 — Web config (done)
- [x] Config page, mode/URLs/question/cooldown/brightness/logo/WiFi/OTA

## Fase 6 — Cleanup (done)
- [x] Remove 13 dead radar handlers, fix pre-existing bugs

## Fase 7 — Remote management (done)
- [x] HTTP Basic Auth on config/logo/update (user "admin")
- [x] Password in NVS; first boot forces password setup
- [x] /stats and /api/stats public
- [x] Web OTA at /update behind auth
- [x] Remote config pull (HTTPS, Bearer token, versioned JSON)
- [x] Config page shows remote version + last fetch

## Fase 8 — Hosted survey page (todo)
- [ ] web/survey/index.html — self-contained mobile survey page
