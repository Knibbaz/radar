# Capsule Radar — web app

The Capsule Radar scope as a browser app: live ADS-B traffic from
[airplanes.live](https://airplanes.live) on a phosphor-green radar, built for
running fullscreen on an Android tablet served from Termux. No backend — the
browser talks to the API directly (it sends `Access-Control-Allow-Origin: *`).

The visual spec is `../assets/plane_radar_2.0_mockup.html`; the projection,
data model and API handling are ports of the firmware's `src/geo.h`,
`src/aircraft.h` and `src/adsb_client.cpp`.

## Develop (on a computer)

```bash
cd webapp
npm install
npm run dev            # http://localhost:5173
npm run dev -- --host  # …and reachable from the tablet on your LAN
```

## Deploy to a tablet via Termux

1. Build on your computer:
   ```bash
   npm run build        # output in webapp/dist/
   ```
2. Copy `dist/` to the tablet (scp to Termux's sshd, Syncthing, or USB).
3. In Termux:
   ```bash
   pkg install python
   cd ~/radar-dist      # wherever you put dist/
   python -m http.server 8080
   ```
4. Open `http://localhost:8080` in Chrome on the tablet → menu →
   **Add to Home screen**. That installs it as a fullscreen PWA (the service
   worker precaches the app shell; after redeploying, reopen the app once or
   twice to pick up the update).

Tip: keep Termux alive while serving (`termux-wake-lock`).

## Settings

Gear button (bottom right): home lat/lon (or **Use device GPS**), display
range (10–100 km), units (aviation/metric), poll interval. Stored in
`localStorage`, so they survive restarts.

## API etiquette

airplanes.live is free for non-commercial use. The app polls at most once per
2 s, only while visible (polling pauses when the tab/PWA is hidden), and asks
only for the radius it displays.
