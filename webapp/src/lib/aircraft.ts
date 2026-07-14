// Aircraft data model + presentation helpers — port of src/aircraft.h (firmware),
// extended with interpolation state for the canvas glide.

export interface Aircraft {
  hex: string;            // ICAO 24-bit id (stable key)
  flight: string;         // callsign
  type: string;           // e.g. "B738" (when available)
  lat: number;
  lon: number;
  altBaroFt: number;      // 0 if on ground
  onGround: boolean;
  track: number | null;   // ground track deg (fallback true_heading already applied)
  gsKt: number | null;    // ground speed kt
  baroRateFpm: number | null; // vertical rate fpm
  squawk: number;         // -1 unknown
  seenPos: number;        // s since last position (API)
  military: boolean;
  lastUpdateMs: number;   // performance.now() at parse (expiry)
  // interpolation state: glide from prev toward lat/lon starting at animStartMs
  prevLat: number;
  prevLon: number;
  animStartMs: number;
}

export const isEmergency = (squawk: number): boolean =>
  squawk === 7500 || squawk === 7600 || squawk === 7700;

// Altitude color map (same bands/bytes as altitudeColor565 / the mockup legend).
export function altitudeColor(altFt: number, onGround: boolean): string {
  if (onGround) return '#888888';
  if (altFt < 3000) return '#ff5a3c';  // red
  if (altFt < 10000) return '#ffb23c'; // amber
  if (altFt < 20000) return '#c8ff3c'; // lime
  if (altFt < 30000) return '#39ff14'; // green
  return '#3ce0ff';                    // cyan
}

// Eased position between the previous and latest fix (ease-out quad, matches
// the firmware glide in radar_view.cpp). pollMs is the measured poll cadence.
export function interpolatedPos(ac: Aircraft, nowMs: number, pollMs: number): { lat: number; lon: number } {
  const t = Math.min(1, Math.max(0, (nowMs - ac.animStartMs) / pollMs));
  const e = t * (2 - t);
  return {
    lat: ac.prevLat + (ac.lat - ac.prevLat) * e,
    lon: ac.prevLon + (ac.lon - ac.prevLon) * e,
  };
}
