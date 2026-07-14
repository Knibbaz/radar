// Tunables — mirrors the relevant defaults from src/config.h (firmware).

export const HOME_DEFAULT = { lat: 38.8409, lon: 0.1059 }; // Dénia, Spain

export const RANGE_STEPS_KM = [10, 20, 30, 50, 100];
export const RANGE_KM_DEFAULT = 30;

export const POLL_INTERVAL_MS = 2000;   // be gentle with the free API (>= 2000)
export const FETCH_TIMEOUT_MS = 8000;
export const AC_STALE_MS = 15000;       // drop aircraft not seen in a poll for this long
export const SEEN_POS_MAX_S = 60;       // skip feed entries whose position is older than this
export const MAX_AIRCRAFT = 60;         // keep only the nearest N per poll

export const TAP_RADIUS_PX = 40;        // finger-tap catch radius (radar_view.cpp)
export const ZOOM_ANIM_MS = 450;        // range-change ease duration
export const SWEEP_PERIOD_MS = 4200;    // mockup sweep rotation period
export const SNAP_KM = 2;               // glide snap guard: jump > this teleports instead

// Query the feed wider than the display range so zooming out has data ready
// (firmware queries 50 km for a 30 km scope).
export const queryRadiusKm = (rangeKm: number): number =>
  Math.max(rangeKm * 1.5, rangeKm + 20);
