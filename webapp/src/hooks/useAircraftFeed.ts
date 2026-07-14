// Polling loop: fetch the feed, merge into a Map the render loop reads by ref
// (no React re-render per frame), expire stale aircraft, pause while hidden.

import { useEffect, useRef, useState } from 'react';
import { fetchAircraft } from '../lib/adsb';
import { interpolatedPos, type Aircraft } from '../lib/aircraft';
import { haversineKm } from '../lib/geo';
import { AC_STALE_MS, FETCH_TIMEOUT_MS, SNAP_KM, queryRadiusKm } from '../lib/config';
import type { Settings } from './useSettings';

export interface FeedStatus {
  count: number;
  lastPollOk: boolean;
  pollTick: number; // bumps once per poll; drives HUD/DetailCard re-render
}

export function useAircraftFeed(settings: Settings) {
  const storeRef = useRef(new Map<string, Aircraft>());
  const pollMsRef = useRef(settings.pollIntervalMs); // measured cadence, clocks the glide
  const [status, setStatus] = useState<FeedStatus>({ count: 0, lastPollOk: true, pollTick: 0 });

  const { homeLat, homeLon, rangeKm, pollIntervalMs } = settings;

  useEffect(() => {
    let cancelled = false;
    let timer: number | undefined;
    let ctrl: AbortController | null = null;
    let lastMergeMs = 0;

    const merge = (fresh: Aircraft[]) => {
      const store = storeRef.current;
      const now = performance.now();
      // Clock the glide off the actual cadence (clamped like the firmware).
      pollMsRef.current = Math.min(8000, Math.max(400, lastMergeMs ? now - lastMergeMs : pollIntervalMs));
      lastMergeMs = now;
      for (const ac of fresh) {
        const old = store.get(ac.hex);
        if (old) {
          // Start the glide from the currently *shown* (interpolated) position
          // so a slow poll doesn't cause a visual jump; teleport on big jumps.
          const p = interpolatedPos(old, now, pollMsRef.current);
          if (haversineKm(p.lat, p.lon, ac.lat, ac.lon) <= SNAP_KM) {
            ac.prevLat = p.lat;
            ac.prevLon = p.lon;
          }
        }
        store.set(ac.hex, ac);
      }
      for (const [hex, ac] of store) {
        if (now - ac.lastUpdateMs > AC_STALE_MS) store.delete(hex);
      }
      if (!cancelled) setStatus((s) => ({ count: store.size, lastPollOk: true, pollTick: s.pollTick + 1 }));
    };

    const attempt = () => {
      ctrl = new AbortController();
      const c = ctrl;
      const t = window.setTimeout(() => c.abort(), FETCH_TIMEOUT_MS);
      return fetchAircraft({ lat: homeLat, lon: homeLon }, queryRadiusKm(rangeKm), c.signal)
        .finally(() => window.clearTimeout(t));
    };

    const poll = async () => {
      try {
        let fresh: Aircraft[];
        try {
          fresh = await attempt();
        } catch (e) {
          if (cancelled || document.hidden) throw e;
          fresh = await attempt(); // one immediate retry (matches the firmware)
        }
        if (!cancelled) merge(fresh);
      } catch {
        // keep the last good aircraft; HUD dot goes amber
        if (!cancelled) setStatus((s) => ({ ...s, lastPollOk: false, pollTick: s.pollTick + 1 }));
      }
      if (!cancelled && !document.hidden) timer = window.setTimeout(poll, pollIntervalMs);
    };

    // API etiquette: never poll while the tab/PWA is hidden (tablet gets locked a lot).
    const onVisibility = () => {
      window.clearTimeout(timer);
      if (document.hidden) ctrl?.abort();
      else void poll();
    };
    document.addEventListener('visibilitychange', onVisibility);

    void poll();
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
      ctrl?.abort();
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, [homeLat, homeLon, rangeKm, pollIntervalMs]);

  return { storeRef, pollMsRef, status };
}
