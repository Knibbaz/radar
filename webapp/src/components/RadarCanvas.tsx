// Canvas host: rAF render loop, devicePixelRatio scaling, tap hit-testing.
// Reads the aircraft Map by ref each frame (no React re-render per frame).

import { useEffect, useRef } from 'react';
import { altitudeColor, interpolatedPos, isEmergency, type Aircraft } from '../lib/aircraft';
import { bearingDeg, haversineKm, projectToScreen } from '../lib/geo';
import { TAP_RADIUS_PX } from '../lib/config';
import { drawScope, scopeGeom, type Blip } from '../render/scope';
import type { Settings } from '../hooks/useSettings';

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
      const blips: Blip[] = [];
      for (const ac of storeRef.current.values()) {
        const p = interpolatedPos(ac, now, pollMsRef.current);
        const distKm = haversineKm(st.homeLat, st.homeLon, p.lat, p.lon);
        const brg = bearingDeg(st.homeLat, st.homeLon, p.lat, p.lon);
        const sp = projectToScreen(distKm, brg, st.rangeKm, g.cx, g.cy, g.rOuter);
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
      drawScope(ctx, g, now, st.rangeKm, blips, sel);
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
