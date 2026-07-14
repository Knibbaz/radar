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

export type FlightType = 'commercial' | 'cargo' | 'military' | 'private' | 'other';

// Classify aircraft by type based on callsign/type code patterns
export function classifyFlightType(ac: Aircraft): FlightType {
  if (ac.military) return 'military';

  const callsign = ac.flight?.toUpperCase() || '';
  const type = ac.type?.toUpperCase() || '';

  // Cargo indicators (callsigns like "FDX", "UPS", "DHL" or types containing cargo codes)
  if (/^(FDX|UPS|DHL|ABX|AMS|ASY|ATI|CAI|CKS|CVA|EVA|FFT|FTW|GTI|HAG|ICT|KLM|LOT|NPT|PIA|SWR|TAP|THY|UZB|VGO|VIR|VLI)/.test(callsign)) {
    return 'cargo';
  }
  if (/CARGO|FREIGHTER|TANKER/.test(type)) return 'cargo';

  // Private/charter (callsigns starting with certain prefixes)
  if (/^(PVT|PRI|CHR|TCS)/.test(callsign)) return 'private';

  // Commercial (known airline callsigns - sample, can be extended)
  if (/^(AAL|DAL|UAL|SWA|ASA|BAW|DLH|AFR|KLM|IBE|ITA|AZA|SAS|LOT|CSA|TAP|AUA|LUF|RYR|EZY|VIR|GIA|QFA|SIA|CCA|ANA|JAL|KAL|CES|UAE|EYG)/.test(callsign)) {
    return 'commercial';
  }

  // Default: if it looks commercial (2-letter IATA + numbers), classify as commercial
  if (/^[A-Z]{2}\d{1,4}$/.test(callsign) && !ac.military && callsign.length <= 6) {
    return 'commercial';
  }

  return 'other';
}

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
