# Radar Webapp Feature Backlog

## Priority Levels
**P1 — Finish in progress:**
- Smooth aircraft movement (improves core responsiveness)
- Radar sweep hit detection (enables selection feel)

**P2 — Core radar polish:**
- Night mode (easy win, big visual impact)
- Flight type filter (high value)
- History playback (powerful learning tool)

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

## In Progress
- [ ] Smooth aircraft movement (animatie tussen posities)
- [ ] Radar sweep hit detection (updates bij sweep contact)

## Done
- [ ] Basic radar visualization
- [ ] Aircraft selection and details
- [ ] Range control with smooth zoom
- [ ] Settings persistence
- [ ] Responsive design for mobile/desktop
