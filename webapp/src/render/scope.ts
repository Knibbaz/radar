// Pure canvas drawing for the phosphor scope — no React, no geo. All inputs are
// pixel-space. Geometry follows the 466x466 mockup (assets/plane_radar_2.0_mockup.html):
// every mockup coordinate is multiplied by s = size/466.

import { SWEEP_PERIOD_MS } from '../lib/config';

export interface ScopeGeom {
  size: number;   // CSS px, square
  cx: number;
  cy: number;
  rOuter: number; // outer ring radius (218/233 of size/2)
}

interface SweepState {
  angleRad: number; // current sweep angle (rad, 0 = north)
  prevAngleRad: number; // previous frame's angle, for wrap detection
  lastGridCell: number; // track which grid cell the sweep is in (0-7 for 45° sectors)
}

export interface Blip {
  hex: string;
  x: number;
  y: number;
  track: number | null;
  color: string;
  callsign: string; // may be empty
  altText: string;  // "11025 ft" / "GND"
  emergency: boolean;
}

export const scopeGeom = (size: number): ScopeGeom => ({
  size,
  cx: size / 2,
  cy: size / 2,
  rOuter: (size / 2) * (218 / 233),
});

const GREEN = '#1dff86';
const TAU = Math.PI * 2;
const DEG = Math.PI / 180;
// Aircraft glyph from the mockup, pointing north at scale 1.
let GLYPH: Path2D | null = null;
function getGlyph(): Path2D {
  if (!GLYPH && typeof Path2D !== 'undefined') {
    GLYPH = new Path2D('M0,-9 L2,-1 L9,3 L2,3 L3,8 L0,6.5 L-3,8 L-2,3 L-9,3 L-2,-1 Z');
  }
  return GLYPH!;
}

const mono = (px: number) => `${px}px 'Share Tech Mono', ui-monospace, monospace`;

function drawChrome(ctx: CanvasRenderingContext2D, g: ScopeGeom, s: number, rangeKm: number) {
  const { cx, cy } = g;

  // vignette (mockup #vign)
  const vign = ctx.createRadialGradient(cx, cy, 0, cx, cy, g.size / 2);
  vign.addColorStop(0.68, '#000000');
  vign.addColorStop(1, '#04140b');
  ctx.fillStyle = vign;
  ctx.beginPath();
  ctx.arc(cx, cy, g.size / 2, 0, TAU);
  ctx.fill();

  // rings
  ctx.strokeStyle = GREEN;
  ctx.lineWidth = 1;
  const rings: Array<[number, number]> = [[218, 0.34], [160, 0.26], [104, 0.26], [50, 0.26]];
  for (const [r, alpha] of rings) {
    ctx.globalAlpha = alpha;
    ctx.beginPath();
    ctx.arc(cx, cy, r * s, 0, TAU);
    ctx.stroke();
  }

  // crosshair
  ctx.globalAlpha = 0.16;
  ctx.beginPath();
  ctx.moveTo(cx, 22 * s);
  ctx.lineTo(cx, 444 * s);
  ctx.moveTo(22 * s, cy);
  ctx.lineTo(444 * s, cy);
  ctx.stroke();
  ctx.globalAlpha = 1;

  // compass
  ctx.textAlign = 'center';
  ctx.font = mono(21 * s);
  ctx.fillStyle = '#eafff3';
  ctx.fillText('N', cx, 40 * s);
  ctx.font = mono(18 * s);
  ctx.fillStyle = '#9affc8';
  ctx.fillText('S', cx, 441 * s);
  ctx.fillText('W', 35 * s, 239 * s);
  ctx.fillText('E', 431 * s, 239 * s);

  // range label at the mid ring (mockup: "15 km" for a 30 km scope)
  ctx.textAlign = 'left';
  ctx.font = mono(11 * s);
  ctx.fillStyle = GREEN;
  ctx.globalAlpha = 0.5;
  ctx.fillText(`${Math.round(rangeKm / 2)} km`, 318 * s, 226 * s);
  ctx.globalAlpha = 1;
}

function drawSweep(ctx: CanvasRenderingContext2D, g: ScopeGeom, s: number, nowMs: number) {
  const { cx, cy, rOuter } = g;
  const lead = ((nowMs % SWEEP_PERIOD_MS) / SWEEP_PERIOD_MS) * TAU; // 0 = north, clockwise
  const wedge = 40 * DEG;
  // canvas angles: 0 = +x axis; our 0 = up
  const a0 = lead - wedge - Math.PI / 2;
  const a1 = lead - Math.PI / 2;

  ctx.save();
  if (typeof ctx.createConicGradient === 'function') {
    const grad = ctx.createConicGradient(a0, cx, cy);
    const w = wedge / TAU;
    grad.addColorStop(0, 'rgba(29,255,134,0)');
    grad.addColorStop(w * 0.78, 'rgba(29,255,134,0.09)');
    grad.addColorStop(w, 'rgba(29,255,134,0.30)');
    grad.addColorStop(Math.min(1, w + 0.002), 'rgba(29,255,134,0)');
    ctx.fillStyle = grad;
  } else {
    ctx.fillStyle = 'rgba(29,255,134,0.10)';
  }
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.arc(cx, cy, rOuter + 2 * s, a0, a1);
  ctx.closePath();
  ctx.fill();

  // bright leading line with glow
  ctx.strokeStyle = '#3dff9a';
  ctx.globalAlpha = 0.85;
  ctx.lineWidth = 2 * s;
  ctx.shadowColor = '#3dff9a';
  ctx.shadowBlur = 6 * s;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + rOuter * Math.sin(lead), cy - rOuter * Math.cos(lead));
  ctx.stroke();
  ctx.restore();
}

function drawBlip(ctx: CanvasRenderingContext2D, g: ScopeGeom, s: number, nowMs: number, b: Blip) {
  ctx.save();
  ctx.translate(b.x, b.y);

  if (b.emergency) {
    // pulsing halo (mockup: r 10 -> 26, 1.1 s)
    const p = (nowMs % 1100) / 1100;
    ctx.strokeStyle = '#ff5a3c';
    ctx.globalAlpha = 0.9 * (1 - p);
    ctx.lineWidth = (2.4 - 1.9 * p) * s;
    ctx.beginPath();
    ctx.arc(0, 0, (10 + 16 * p) * s, 0, TAU);
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  ctx.save();
  ctx.rotate((b.track ?? 0) * DEG);
  ctx.scale(s, s);
  ctx.fillStyle = b.color;
  ctx.shadowColor = b.color;
  ctx.shadowBlur = 3; // in glyph units; cheap glow like the mockup filter
  ctx.fill(getGlyph());
  ctx.restore();

  // labels: to the right, flipped left near the right rim so they stay on-screen
  const flip = b.x > g.cx + g.rOuter * 0.55;
  ctx.textAlign = flip ? 'right' : 'left';
  const lx = (flip ? -11 : 11) * s;
  if (b.callsign) {
    ctx.font = mono(12 * s);
    ctx.fillStyle = '#dfeee6';
    ctx.fillText(b.callsign, lx, -3 * s);
  }
  ctx.font = mono(10 * s);
  ctx.fillStyle = b.color;
  ctx.fillText(b.altText, lx, 10 * s);
  ctx.restore();
}

function drawSelection(ctx: CanvasRenderingContext2D, s: number, nowMs: number, b: Blip) {
  // opacity pulse 0.95 <-> 0.4 at 1.9 s (mockup #selRing)
  const pulse = 0.4 + 0.55 * (0.5 + 0.5 * Math.cos(TAU * (nowMs % 1900) / 1900));
  ctx.strokeStyle = b.emergency ? '#ff5a3c' : '#ffffff';
  ctx.globalAlpha = pulse;
  ctx.lineWidth = 1.6 * s;
  ctx.beginPath();
  ctx.arc(b.x, b.y, 15 * s, 0, TAU);
  ctx.stroke();
  ctx.globalAlpha = 1;
}

function drawCenter(ctx: CanvasRenderingContext2D, g: ScopeGeom, s: number, nowMs: number) {
  const { cx, cy } = g;
  // expanding pulse ring (mockup: r 5 -> 28, 2.6 s)
  const p = (nowMs % 2600) / 2600;
  ctx.strokeStyle = '#eafff3';
  ctx.globalAlpha = 0.9 * (1 - p);
  ctx.lineWidth = 1.4 * s;
  ctx.beginPath();
  ctx.arc(cx, cy, (5 + 23 * p) * s, 0, TAU);
  ctx.stroke();
  ctx.globalAlpha = 1;
  ctx.fillStyle = '#eafff3';
  ctx.beginPath();
  ctx.arc(cx, cy, 3.2 * s, 0, TAU);
  ctx.fill();
}

function normalizeAngle(rad: number): number {
  let a = rad % TAU;
  return a < 0 ? a + TAU : a;
}

function angleInWedge(blipAngleRad: number, sweepAngleRad: number, wedgeRad: number): boolean {
  const diff = normalizeAngle(blipAngleRad - sweepAngleRad);
  return diff <= wedgeRad || diff >= TAU - wedgeRad;
}

export function detectSweepHits(
  nowMs: number,
  blips: Blip[],
  g: ScopeGeom,
  sweepState: SweepState,
): Set<string> {
  const sweepAngleRad = ((nowMs % SWEEP_PERIOD_MS) / SWEEP_PERIOD_MS) * TAU; // 0 = north
  const wedgeRad = (40 * DEG) / 2; // half-width of sweep wedge

  const hitsNow = new Set<string>();

  // For many aircraft, use grid-based detection (8 cells = 45° sectors)
  // to avoid audio spam. For few aircraft, detect every blip.
  const useGridDetection = blips.length >= 20;

  if (useGridDetection) {
    // Calculate which 45° grid cell the sweep is in (0-7)
    const gridCellSize = TAU / 8;
    const currentGridCell = Math.floor(sweepAngleRad / gridCellSize) % 8;

    // Only trigger if sweep entered a new grid cell
    if (currentGridCell !== sweepState.lastGridCell) {
      // Find if any aircraft are in this cell
      for (const b of blips) {
        const dx = b.x - g.cx;
        const dy = b.y - g.cy;
        const blipAngleRad = Math.atan2(dx, -dy);
        const blipGridCell = Math.floor(normalizeAngle(blipAngleRad) / gridCellSize) % 8;
        if (blipGridCell === currentGridCell) {
          hitsNow.add(b.hex);
        }
      }
      sweepState.lastGridCell = currentGridCell;
    }
  } else {
    // For few aircraft, detect every blip in sweep wedge
    for (const b of blips) {
      const dx = b.x - g.cx;
      const dy = b.y - g.cy;
      const blipAngleRad = Math.atan2(dx, -dy);
      if (angleInWedge(blipAngleRad, sweepAngleRad, wedgeRad)) {
        hitsNow.add(b.hex);
      }
    }
  }

  sweepState.prevAngleRad = sweepState.angleRad;
  sweepState.angleRad = sweepAngleRad;
  return hitsNow;
}

export function drawScope(
  ctx: CanvasRenderingContext2D,
  g: ScopeGeom,
  nowMs: number,
  rangeKm: number,
  blips: Blip[],
  selectedHex: string | null,
) {
  const s = g.size / 466;
  ctx.clearRect(0, 0, g.size, g.size);
  ctx.save();
  ctx.beginPath();
  ctx.arc(g.cx, g.cy, g.size / 2, 0, TAU);
  ctx.clip();

  drawChrome(ctx, g, s, rangeKm);
  drawSweep(ctx, g, s, nowMs);
  for (const b of blips) drawBlip(ctx, g, s, nowMs, b);
  const sel = selectedHex ? blips.find((b) => b.hex === selectedHex) : undefined;
  if (sel) drawSelection(ctx, s, nowMs, sel);
  drawCenter(ctx, g, s, nowMs);

  ctx.restore();
}
