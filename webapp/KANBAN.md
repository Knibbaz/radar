# Radar Webapp Feature Backlog

## Priority Levels
**P1 — Finish in progress:** ✅
- Smooth aircraft movement (animStartMs reset on updates)
- Radar sweep hit detection (grid-based for 20+ aircraft, configurable)

**P2 — Core radar polish:** ✅ (night mode, filter done)
- [x] Night mode (red scanlines on black)
- [x] Flight type filter (commercial, cargo, military, private, other)
- [x] Nerd mode (toggle for nm, ft, kt always)
- [ ] History playback (powerful learning tool)

**P3 — Touch interaction:**
- Pinch-to-zoom gesture
- Long-press for detail
- Swipe for view switching

**P4 — Aircraft detail:**
- Live route visualization (render existing data)
- Flight specs/telemetry overlay
- Aircraft photos (engagement)

**P5 — Nerdy extras:**
- Signal strength, metrics, anomaly detection
- ML-based detection (future)

---

## To Do
### Touch Screen Features
- [ ] Pinch-to-zoom en rotate gebaren
- [ ] Dubbel tikken voor quick zoom
- [ ] Lang tikken voor extended aircraft info
- [ ] Swipe gebaren voor view switching
- [ ] Tactiele feedback bij aircraft selectie
- [ ] Screen edge gestures voor snelmenu

### Aircraft Details
- [ ] Aircraft photo gallery (op basis van registratienummer)
- [ ] Live aircraft specs (type, snelheid, hoogte histogram) 
- [ ] Live route visualization (met API calls naar FlightAware/OpenSky)
- [ ] 3D model viewer (basis van type code)
- [ ] Flight type filter (commercieel/vracht/militair/prive/overig)
- [ ] Flight route visualization (met waypoints)
- [ ] Detailed telemetry overlay (VS, GS, heading)
- [ ] Emergency squawk analyzer

### Radar Features
- [ ] Night mode (donkere thema + rode scanlines)
- [ ] History playback (tijdlijn slider)
- [ ] Predictive trails (projected path)
- [ ] Weather radar overlay
- [ ] Noise filter voor kleine vliegtuigen
- [ ] ADS-B raw data view

### Nerdy Extras
- [ ] **NERD MODE** (show technical details)
  - Display range in nm only (not km)
  - Display altitudes in feet only (override metric mode)
  - Display speeds in kt only (override metric mode)
  - Show ICAO hex (6-digit) in glyph hover
  - Show squawk as octal + decimal (e.g., 1234 = 2322 oct)
  - Show data age (seen_pos seconds)
  - Show exact lat/lon to 4 decimals
  - Show vert. speed in fpm (ft/min) not m/s
  - Show track/heading + magnetic variance
  - Display poll cadence (measured vs target)
  - Maybe: show ADS-B signal info if available
- [ ] API response time metrics
- [ ] Signal strength visualization  
- [ ] Aircraft database sync (via OpenSky)
- [ ] Custom PSR/SSR modes
- [ ] Antenna bearing calculator
- [ ] ML-based anomaly detection

### General
- [ ] Airport database (ICAO codes, names)
- [ ] Custom waypoints
- [ ] Multi-language support

### Bug Fixes (Completed)
- [x] Unit conversion (feet/meters, nm/km) in DetailCard and HUD
- [x] Sweep detection configuration via settings panel
- [x] Grid-based sweep detection for 20+ aircraft

## In Progress

## Done
- [x] Basic radar visualization
- [x] Aircraft selection and details
- [x] Range control with smooth zoom
- [x] Settings persistence
- [x] Responsive design for mobile/desktop
- [x] Smooth aircraft movement (animatie tussen posities)
- [x] Radar sweep hit detection (updates bij sweep contact)
