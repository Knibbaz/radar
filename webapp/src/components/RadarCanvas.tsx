// Canvas host: rAF render loop, devicePixelRatio scaling, tap hit-testing.
// Reads the aircraft Map by ref each frame (no React re-render per frame).

import { useEffect, useRef } from 'react';
import { altitudeColor, interpolatedPos, isEmergency, type Aircraft } from '../lib/aircraft';
import { bearingDeg, haversineKm, projectToScreen } from '../lib/geo';
import { TAP_RADIUS_PX, ZOOM_ANIM_MS } from '../lib/config';
import { drawScope, scopeGeom, type Blip } from '../render/scope';
import type { Settings } from '../hooks/useSettings';

function animatedRange(z: { from: number; to: number; startMs: number }, nowMs: number): number {
  const t = Math.min(1, Math.max(0, (nowMs - z.startMs) / ZOOM_ANIM_MS));
  const e = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; // ease-in-out cubic
  return z.from + (z.to - z.from) * e;
}

interface Props {
  settings: Settings;
  storeRef: { current: Map<string, Aircraft> };
  pollMsRef: { current: number };
  selectedHex: string | null;
  onSelect: (hex: string | null) => void;
}

export default function RadarCanvas({ settings, storeRef, pollMsRef, selectedHex, onSelect }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sizeRef = useRef({ css: 0, dpr: 1 });
  const blipsRef = useRef<Blip[]>([]); // last frame's blips, for hit-testing

  // latest props for the rAF closure without restarting the loop
  const propsRef = useRef({ settings, selectedHex });
  propsRef.current = { settings, selectedHex };

  // zoom glide: the displayed range eases toward settings.rangeKm
  const zoomRef = useRef({ from: settings.rangeKm, to: settings.rangeKm, startMs: 0 });

  useEffect(() => {
    const wrap = wrapRef.current!;
    const canvas = canvasRef.current!;
    const ro = new ResizeObserver(() => {
      const css = wrap.clientWidth;
      const dpr = window.devicePixelRatio || 1;
      sizeRef.current = { css, dpr };
      canvas.width = Math.round(css * dpr);
      canvas.height = Math.round(css * dpr);
    });
    ro.observe(wrap);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    const ctx = canvasRef.current!.getContext('2d')!;
    let raf = 0;
    const frame = () => {
      raf = requestAnimationFrame(frame);
      const { css, dpr } = sizeRef.current;
      if (!css) return;
      const { settings: st, selectedHex: sel } = propsRef.current;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0); // draw in CSS px

      const g = scopeGeom(css);
      const now = performance.now();

      // ease the displayed range toward the target (retarget mid-glide from
      // the current animated value, so rapid taps stay smooth)
      const z = zoomRef.current;
      if (z.to !== st.rangeKm) {
        z.from = animatedRange(z, now);
        z.to = st.rangeKm;
        z.startMs = now;
      }
      const rangeNow = animatedRange(z, now);

      const blips: Blip[] = [];
      for (const ac of storeRef.current.values()) {
        const p = interpolatedPos(ac, now, pollMsRef.current);
        const distKm = haversineKm(st.homeLat, st.homeLon, p.lat, p.lon);
        const brg = bearingDeg(st.homeLat, st.homeLon, p.lat, p.lon);
        const sp = projectToScreen(distKm, brg, rangeNow, g.cx, g.cy, g.rOuter);
        if (!sp.inRange) continue; // rim arrows are a later phase
        blips.push({
          hex: ac.hex,
          x: sp.x,
          y: sp.y,
          track: ac.track,
          color: altitudeColor(ac.altBaroFt, ac.onGround),
          callsign: ac.flight || ac.hex,
          altText: ac.onGround ? 'GND' : `${Math.round(ac.altBaroFt)} ft`,
          emergency: isEmergency(ac.squawk),
        });
      }
      blipsRef.current = blips;
      drawScope(ctx, g, now, rangeNow, blips, sel);
    };
    raf = requestAnimationFrame(frame); // browser stops rAF while the tab is hidden
    return () => cancelAnimationFrame(raf);
  }, [storeRef, pollMsRef]);

  const onPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const rect = canvasRef.current!.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    let best: Blip | null = null;
    let bestD = TAP_RADIUS_PX * TAP_RADIUS_PX;
    for (const b of blipsRef.current) {
      const d = (b.x - x) * (b.x - x) + (b.y - y) * (b.y - y);
      if (d < bestD) {
        bestD = d;
        best = b;
      }
    }
    onSelect(best ? best.hex : null);
  };

  return (
    <div ref={wrapRef} className="scope">
      <canvas ref={canvasRef} onPointerDown={onPointerDown} />
    </div>
  );
}
