import { altitudeColor, isEmergency, type Aircraft } from '../lib/aircraft';
import { bearingDeg, haversineKm } from '../lib/geo';
import type { Settings } from '../hooks/useSettings';

interface Props {
  aircraft: Aircraft | null;
  settings: Settings;
}

const FT_TO_M = 0.3048;
const KT_TO_KMH = 1.852;
const FPM_TO_MS = 0.00508;

function fmtAlt(ac: Aircraft, metric: boolean): string {
  if (ac.onGround) return 'GND';
  return metric
    ? `${Math.round(ac.altBaroFt * FT_TO_M).toLocaleString()} m`
    : `${Math.round(ac.altBaroFt).toLocaleString()} ft`;
}

function fmtVs(vs: number | null, metric: boolean): string {
  if (vs == null) return '—';
  if (Math.abs(vs) < 64) return '— level';
  const arrow = vs > 0 ? '▲ +' : '▼ −';
  return metric
    ? `${arrow}${Math.abs(vs * FPM_TO_MS).toFixed(1)} m/s`
    : `${arrow}${Math.round(Math.abs(vs))} fpm`;
}

function fmtSpd(gs: number | null, metric: boolean): string {
  if (gs == null) return '—';
  return metric ? `${Math.round(gs * KT_TO_KMH)} km/h` : `${Math.round(gs)} kt`;
}

function fmtDist(km: number, metric: boolean): string {
  return metric ? `${km.toFixed(1)} km` : `${(km * 0.539957).toFixed(1)} nm`;
}

export default function DetailCard({ aircraft: ac, settings }: Props) {
  if (!ac) {
    return (
      <div className="legend">
        <span className="lt">Altitude</span>
        <div className="bar" />
        <div className="ticks"><span>0</span><span>10k</span><span>20k</span><span>35k+ ft</span></div>
      </div>
    );
  }

  const metric = settings.units === 'metric' && !settings.nerdMode;
  const distKm = haversineKm(settings.homeLat, settings.homeLon, ac.lat, ac.lon);
  const brg = bearingDeg(settings.homeLat, settings.homeLon, ac.lat, ac.lon);
  const emergency = isEmergency(ac.squawk);
  const color = altitudeColor(ac.altBaroFt, ac.onGround);

  return (
    <div className="card">
      <div className="card-head">
        <span className="dot" style={{ background: color, boxShadow: `0 0 9px ${color}` }} />
        <span className="call">{ac.flight || ac.hex.toUpperCase()}</span>
        {ac.type && <span className="badge">{ac.type}</span>}
        {ac.military && <span className="badge">MIL</span>}
      </div>
      <div className="grid">
        <div><label>Altitude</label><b>{fmtAlt(ac, metric)}</b></div>
        <div><label>Vert. speed</label><b>{fmtVs(ac.baroRateFpm, metric)}</b></div>
        <div><label>Speed</label><b>{fmtSpd(ac.gsKt, metric)}</b></div>
        <div><label>Heading</label><b>{ac.track != null ? `${Math.round(ac.track)}°` : '—'}</b></div>
        <div><label>Distance</label><b>{fmtDist(distKm, metric)} · {Math.round(brg)}°</b></div>
        <div>
          <label>Squawk</label>
          <b className={emergency ? 'sq-alert' : undefined}>
            {ac.squawk >= 0 ? String(ac.squawk).padStart(4, '0') : '—'}{emergency ? ' ⚠' : ''}
          </b>
        </div>
      </div>
    </div>
  );
}
