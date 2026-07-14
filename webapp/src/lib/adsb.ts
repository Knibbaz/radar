// Fetch + parse the airplanes.live point feed — port of src/adsb_client.cpp.
//
// Etiquette: the API is free for non-commercial use. Browsers cannot set a
// User-Agent header, so we honor it by keeping the poll cadence >= 2 s and
// querying only the radius we display.
//
// FALLBACK: api.adsb.lol serves the same /v2/point format, but sent no CORS
// headers as of 2026-07, so a browser-side fallback is dead code. Re-test later.

import { haversineKm, kmToNm } from './geo';
import { MAX_AIRCRAFT, SEEN_POS_MAX_S } from './config';
import type { Aircraft } from './aircraft';

const API_HOST = 'https://api.airplanes.live';

interface RawAc {
  hex?: string;
  flight?: string;
  t?: string;
  lat?: number;
  lon?: number;
  alt_baro?: number | string; // "ground" when on the ground
  track?: number;
  true_heading?: number;
  gs?: number;
  baro_rate?: number;
  squawk?: string | number;   // arrives as a string, e.g. "7700"
  seen_pos?: number;
  dbFlags?: number;           // bit 0 = military
}

export async function fetchAircraft(
  home: { lat: number; lon: number },
  queryKm: number,
  signal: AbortSignal,
): Promise<Aircraft[]> {
  const radiusNm = Math.max(1, Math.round(kmToNm(queryKm)));
  const url = `${API_HOST}/v2/point/${home.lat.toFixed(4)}/${home.lon.toFixed(4)}/${radiusNm}`;
  const res = await fetch(url, { signal, headers: { Accept: 'application/json' } });
  if (!res.ok) throw new Error(`feed HTTP ${res.status}`);
  const json = await res.json();
  const raw: RawAc[] = json.ac ?? json.aircraft ?? [];

  const now = performance.now();
  const out: Aircraft[] = [];
  for (const r of raw) {
    if (typeof r.lat !== 'number' || typeof r.lon !== 'number') continue;
    if ((r.seen_pos ?? 0) > SEEN_POS_MAX_S) continue; // position too old to plot honestly
    const sq = r.squawk != null ? parseInt(String(r.squawk), 10) : NaN;
    out.push({
      hex: (r.hex ?? '').trim(),
      flight: (r.flight ?? '').trim(),
      type: (r.t ?? '').trim(),
      lat: r.lat,
      lon: r.lon,
      altBaroFt: typeof r.alt_baro === 'number' ? r.alt_baro : 0,
      onGround: r.alt_baro === 'ground',
      track: r.track ?? r.true_heading ?? null,
      gsKt: r.gs ?? null,
      baroRateFpm: r.baro_rate ?? null,
      squawk: Number.isNaN(sq) ? -1 : sq,
      seenPos: r.seen_pos ?? 0,
      military: ((r.dbFlags ?? 0) & 1) !== 0,
      lastUpdateMs: now,
      prevLat: r.lat,
      prevLon: r.lon,
      animStartMs: now,
    });
  }

  // Keep only the nearest N (the firmware's RAM cap; here it just bounds draw work).
  out.sort(
    (a, b) =>
      haversineKm(home.lat, home.lon, a.lat, a.lon) -
      haversineKm(home.lat, home.lon, b.lat, b.lon),
  );
  return out.slice(0, MAX_AIRCRAFT);
}
