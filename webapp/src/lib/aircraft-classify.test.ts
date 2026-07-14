import { describe, it, expect } from 'vitest';
import { classifyFlightType, type Aircraft } from './aircraft';

const baseAircraft: Aircraft = {
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
  lastUpdateMs: 0,
  prevLat: 40.0,
  prevLon: -74.0,
  animStartMs: 0,
};

describe('classifyFlightType', () => {
  it('classifies military aircraft', () => {
    const ac: Aircraft = { ...baseAircraft, military: true };
    expect(classifyFlightType(ac)).toBe('military');
  });

  it('classifies cargo by callsign (FDX)', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'FDX1234' };
    expect(classifyFlightType(ac)).toBe('cargo');
  });

  it('classifies cargo by callsign (UPS)', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'UPS456' };
    expect(classifyFlightType(ac)).toBe('cargo');
  });

  it('classifies cargo by type code', () => {
    const ac: Aircraft = { ...baseAircraft, type: 'FREIGHTER', flight: 'XYZ123' };
    expect(classifyFlightType(ac)).toBe('cargo');
  });

  it('classifies private by callsign', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'PVT123' };
    expect(classifyFlightType(ac)).toBe('private');
  });

  it('classifies commercial by callsign (AAL)', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'AAL123' };
    expect(classifyFlightType(ac)).toBe('commercial');
  });

  it('classifies commercial by callsign (DAL)', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'DAL456' };
    expect(classifyFlightType(ac)).toBe('commercial');
  });

  it('classifies commercial by standard IATA pattern', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'BA789' };
    expect(classifyFlightType(ac)).toBe('commercial');
  });

  it('defaults to other for unknown callsigns', () => {
    const ac: Aircraft = { ...baseAircraft, flight: 'XYZ123456789' };
    expect(classifyFlightType(ac)).toBe('other');
  });

  it('defaults to other when flight is empty', () => {
    const ac: Aircraft = { ...baseAircraft, flight: '' };
    expect(classifyFlightType(ac)).toBe('other');
  });
});
