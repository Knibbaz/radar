import { useCallback, useState } from 'react';
import { HOME_DEFAULT, POLL_INTERVAL_MS, RANGE_KM_DEFAULT } from '../lib/config';

export type FlightTypeFilter = 'all' | 'commercial' | 'cargo' | 'military' | 'private' | 'other';

export interface Settings {
  homeLat: number;
  homeLon: number;
  rangeKm: number;
  units: 'aviation' | 'metric';
  pollIntervalMs: number;
  sweepDetectionEnabled: boolean;
  nightMode: boolean;
  flightTypeFilter: FlightTypeFilter;
}

const KEY = 'radar.settings';

const DEFAULTS: Settings = {
  homeLat: HOME_DEFAULT.lat,
  homeLon: HOME_DEFAULT.lon,
  rangeKm: RANGE_KM_DEFAULT,
  units: 'aviation',
  pollIntervalMs: POLL_INTERVAL_MS,
  sweepDetectionEnabled: true,
  nightMode: false,
  flightTypeFilter: 'all',
};

function load(): Settings {
  try {
    // merge over defaults so old stored blobs stay valid when fields are added
    return { ...DEFAULTS, ...JSON.parse(localStorage.getItem(KEY) ?? '{}') };
  } catch {
    return DEFAULTS;
  }
}

export function useSettings() {
  const [settings, setState] = useState<Settings>(load);
  const setSettings = useCallback((patch: Partial<Settings>) => {
    setState((prev) => {
      const next = { ...prev, ...patch };
      localStorage.setItem(KEY, JSON.stringify(next));
      return next;
    });
  }, []);
  return { settings, setSettings };
}
