import { useEffect, useState } from 'react';
import { RANGE_STEPS_KM } from '../lib/config';

interface Props {
  count: number;
  feedOk: boolean;
  rangeKm: number;
  onRange: (km: number) => void;
}

export default function Hud({ count, feedOk, rangeKm, onRange }: Props) {
  const [clock, setClock] = useState('');
  useEffect(() => {
    const tick = () =>
      setClock(new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }));
    tick();
    const t = window.setInterval(tick, 1000);
    return () => window.clearInterval(t);
  }, []);

  const idx = RANGE_STEPS_KM.indexOf(rangeKm);
  const step = (dir: -1 | 1) => {
    const next = RANGE_STEPS_KM[Math.min(RANGE_STEPS_KM.length - 1, Math.max(0, idx + dir))];
    if (next !== rangeKm) onRange(next);
  };

  return (
    <>
      <div className="hud">
        <span className="grp">
          <span className={feedOk ? 'feed-dot ok' : 'feed-dot stale'} title={feedOk ? 'feed OK' : 'feed stale'} />
          <span>✈ {count}</span>
        </span>
        <span>{clock}</span>
      </div>
      <div className="range-ctl">
        <button onClick={() => step(-1)} disabled={idx <= 0} aria-label="zoom in">−</button>
        <span>{rangeKm} km</span>
        <button onClick={() => step(1)} disabled={idx >= RANGE_STEPS_KM.length - 1} aria-label="zoom out">+</button>
      </div>
      <div className="brand">CAPSULE RADAR</div>
    </>
  );
}
