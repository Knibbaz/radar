import { describe, it, expect } from 'vitest';
import { detectSweepHits, scopeGeom, type Blip } from './scope';
import { SWEEP_PERIOD_MS } from '../lib/config';

describe('detectSweepHits', () => {
  it('detects aircraft in sweep wedge', () => {
    const g = scopeGeom(466);
    const now = 0; // sweep at north (0°)
    const sweepState = { angleRad: 0, prevAngleRad: 0, lastGridCell: -1 };

    const blips: Blip[] = [
      {
        hex: 'AC1',
        x: g.cx,
        y: g.cy - 50, // directly north
        track: 0,
        color: '#fff',
        callsign: 'AA001',
        altText: '10000 ft',
        emergency: false,
      },
      {
        hex: 'AC2',
        x: g.cx + 50,
        y: g.cy, // directly east
        track: 0,
        color: '#fff',
        callsign: 'UA002',
        altText: '20000 ft',
        emergency: false,
      },
    ];

    const hits = detectSweepHits(now, blips, g, sweepState);
    expect(hits.has('AC1')).toBe(true); // north, in wedge
    expect(hits.has('AC2')).toBe(false); // east, outside wedge
  });

  it('returns empty set when no blips in sweep', () => {
    const g = scopeGeom(466);
    const now = SWEEP_PERIOD_MS / 2; // sweep pointing south
    const sweepState = { angleRad: 0, prevAngleRad: 0, lastGridCell: -1 };

    const blips: Blip[] = [
      {
        hex: 'AC1',
        x: g.cx,
        y: g.cy - 50, // north, away from sweep
        track: 0,
        color: '#fff',
        callsign: 'AA001',
        altText: '10000 ft',
        emergency: false,
      },
    ];

    const hits = detectSweepHits(now, blips, g, sweepState);
    expect(hits.size).toBe(0);
  });

  it('updates sweep state angle', () => {
    const g = scopeGeom(466);
    const now = 1000;
    const sweepState = { angleRad: 0, prevAngleRad: 0, lastGridCell: -1 };

    detectSweepHits(now, [], g, sweepState);
    expect(sweepState.angleRad).toBeGreaterThan(0);
    expect(sweepState.prevAngleRad).toBe(0);
  });

  it('detects multiple aircraft in sweep wedge', () => {
    const g = scopeGeom(466);
    const now = 0;
    const sweepState = { angleRad: 0, prevAngleRad: 0, lastGridCell: -1 };

    const blips: Blip[] = [];
    // create blips in a north arc (within sweep)
    for (let angle = -20; angle <= 20; angle += 10) {
      const rad = (angle * Math.PI) / 180;
      blips.push({
        hex: `AC${angle}`,
        x: g.cx + 100 * Math.sin(rad),
        y: g.cy - 100 * Math.cos(rad),
        track: 0,
        color: '#fff',
        callsign: `FL${angle}`,
        altText: '15000 ft',
        emergency: false,
      });
    }

    const hits = detectSweepHits(now, blips, g, sweepState);
    expect(hits.size).toBeGreaterThan(1);
  });

  it('uses grid-based detection for 20+ aircraft', () => {
    const g = scopeGeom(466);
    const now = 0;
    const sweepState = { angleRad: 0, prevAngleRad: 0, lastGridCell: -1 };

    const blips: Blip[] = [];
    // create 20 blips spread around the scope
    for (let i = 0; i < 20; i++) {
      const angle = (i * 360) / 20;
      const rad = (angle * Math.PI) / 180;
      blips.push({
        hex: `AC${i}`,
        x: g.cx + 100 * Math.sin(rad),
        y: g.cy - 100 * Math.cos(rad),
        track: 0,
        color: '#fff',
        callsign: `FL${i}`,
        altText: '15000 ft',
        emergency: false,
      });
    }

    detectSweepHits(now, blips, g, sweepState);
    expect(sweepState.lastGridCell).not.toBe(-1); // grid cell should be set

    // Move sweep to a different grid cell (45°)
    const now2 = (SWEEP_PERIOD_MS / 8) * 1.1; // move to next grid cell
    detectSweepHits(now2, blips, g, sweepState);
    expect(sweepState.lastGridCell).toBeGreaterThanOrEqual(0);
  });
});
