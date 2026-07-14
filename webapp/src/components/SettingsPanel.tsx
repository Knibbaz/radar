import { useState } from 'react';
import { RANGE_STEPS_KM } from '../lib/config';
import type { Settings } from '../hooks/useSettings';

interface Props {
  settings: Settings;
  onSave: (patch: Partial<Settings>) => void;
}

export default function SettingsPanel({ settings, onSave }: Props) {
  const [open, setOpen] = useState(false);
  const [draft, setDraft] = useState<Settings>(settings);
  const [gpsBusy, setGpsBusy] = useState(false);

  const show = () => {
    setDraft(settings);
    setOpen(true);
  };

  const useGps = () => {
    if (!navigator.geolocation) return;
    setGpsBusy(true);
    navigator.geolocation.getCurrentPosition(
      (pos) => {
        setDraft((d) => ({
          ...d,
          homeLat: Number(pos.coords.latitude.toFixed(4)),
          homeLon: Number(pos.coords.longitude.toFixed(4)),
        }));
        setGpsBusy(false);
      },
      () => setGpsBusy(false),
      { enableHighAccuracy: true, timeout: 10000 },
    );
  };

  const save = () => {
    onSave(draft);
    setOpen(false);
  };

  if (!open) {
    return (
      <button className="gear" onClick={show} aria-label="settings">⚙</button>
    );
  }

  return (
    <div className="settings-overlay" onClick={() => setOpen(false)}>
      <div className="settings card" onClick={(e) => e.stopPropagation()}>
        <h2>Settings</h2>
        <label>
          Latitude
          <input
            type="number" step="0.0001" min="-90" max="90" value={draft.homeLat}
            onChange={(e) => setDraft({ ...draft, homeLat: Number(e.target.value) })}
          />
        </label>
        <label>
          Longitude
          <input
            type="number" step="0.0001" min="-180" max="180" value={draft.homeLon}
            onChange={(e) => setDraft({ ...draft, homeLon: Number(e.target.value) })}
          />
        </label>
        <button onClick={useGps} disabled={gpsBusy}>{gpsBusy ? 'Locating…' : 'Use device GPS'}</button>
        <label>
          Range
          <select
            value={draft.rangeKm}
            onChange={(e) => setDraft({ ...draft, rangeKm: Number(e.target.value) })}
          >
            {RANGE_STEPS_KM.map((km) => <option key={km} value={km}>{km} km</option>)}
          </select>
        </label>
        <label>
          Units
          <select
            value={draft.units}
            onChange={(e) => setDraft({ ...draft, units: e.target.value as Settings['units'] })}
          >
            <option value="aviation">Aviation (ft / kt)</option>
            <option value="metric">Metric (m / km/h)</option>
          </select>
        </label>
        <label>
          Update every
          <select
            value={draft.pollIntervalMs}
            onChange={(e) => setDraft({ ...draft, pollIntervalMs: Number(e.target.value) })}
          >
            <option value={2000}>2 s</option>
            <option value={5000}>5 s</option>
            <option value={10000}>10 s</option>
          </select>
        </label>
        <label>
          <input
            type="checkbox"
            checked={draft.sweepDetectionEnabled}
            onChange={(e) => setDraft({ ...draft, sweepDetectionEnabled: e.target.checked })}
          />
          Sweep radar hit detection (audio beep)
        </label>
        <label>
          <input
            type="checkbox"
            checked={draft.nightMode}
            onChange={(e) => setDraft({ ...draft, nightMode: e.target.checked })}
          />
          Night mode (red scanlines)
        </label>
        <div className="settings-actions">
          <button onClick={() => setOpen(false)}>Cancel</button>
          <button className="primary" onClick={save}>Save</button>
        </div>
      </div>
    </div>
  );
}
