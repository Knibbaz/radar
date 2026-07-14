import { describe, it, expect } from 'vitest';
import { interpolatedPos, type Aircraft } from './aircraft';

describe('interpolatedPos', () => {
  it('returns current position at animation start', () => {
    const now = 1000;
    const ac: Aircraft = {
      hex: 'ABC123',
      flight: 'AA001',
      type: 'B738',
      lat: 40.0,
      lon: -74.0,
      altBaroFt: 35000,
      onGround: false,
      track: 90,
      gsKt: 500,
      baroRateFpm: 1000,
      squawk: 1234,
      seenPos: 5,
      military: false,
      lastUpdateMs: now,
      prevLat: 39.9,
      prevLon: -74.1,
      animStartMs: now,
    };

    const result = interpolatedPos(ac, now, 2000);
    expect(result.lat).toBeCloseTo(39.9, 5);
    expect(result.lon).toBeCloseTo(-74.1, 5);
  });

  it('interpolates smoothly between prev and current position', () => {
    const now = 1000;
    const pollMs = 2000;
    const ac: Aircraft = {
      hex: 'ABC123',
      flight: 'AA001',
      type: 'B738',
      lat: 40.0,
      lon: -74.0,
      altBaroFt: 35000,
      onGround: false,
      track: 90,
      gsKt: 500,
      baroRateFpm: 1000,
      squawk: 1234,
      seenPos: 5,
      military: false,
      lastUpdateMs: now,
      prevLat: 39.0,
      prevLon: -75.0,
      animStartMs: now,
    };

    // midway through animation
    const midpoint = interpolatedPos(ac, now + pollMs / 2, pollMs);
    expect(midpoint.lat).toBeGreaterThan(39.0);
    expect(midpoint.lat).toBeLessThan(40.0);
    expect(midpoint.lon).toBeGreaterThan(-75.0);
    expect(midpoint.lon).toBeLessThan(-74.0);
  });

  it('completes animation at poll interval', () => {
    const now = 1000;
    const pollMs = 2000;
    const ac: Aircraft = {
      hex: 'ABC123',
      flight: 'AA001',
      type: 'B738',
      lat: 40.0,
      lon: -74.0,
      altBaroFt: 35000,
      onGround: false,
      track: 90,
      gsKt: 500,
      baroRateFpm: 1000,
      squawk: 1234,
      seenPos: 5,
      military: false,
      lastUpdateMs: now,
      prevLat: 39.0,
      prevLon: -75.0,
      animStartMs: now,
    };

    // at end of animation
    const endPos = interpolatedPos(ac, now + pollMs + 100, pollMs);
    expect(endPos.lat).toBeCloseTo(40.0, 5);
    expect(endPos.lon).toBeCloseTo(-74.0, 5);
  });
});
