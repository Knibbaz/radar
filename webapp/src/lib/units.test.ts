import { describe, it, expect } from 'vitest';

const FT_TO_M = 0.3048;
const KT_TO_KMH = 1.852;
const FPM_TO_MS = 0.00508;
const KM_TO_NM = 0.539957;

describe('Unit conversions', () => {
  describe('altitude', () => {
    it('converts feet to meters', () => {
      const ft = 35000;
      const m = Math.round(ft * FT_TO_M);
      expect(m).toBe(10668);
    });

    it('handles ground level', () => {
      const ft = 0;
      const m = Math.round(ft * FT_TO_M);
      expect(m).toBe(0);
    });
  });

  describe('speed', () => {
    it('converts knots to km/h', () => {
      const kt = 500;
      const kmh = Math.round(kt * KT_TO_KMH);
      expect(kmh).toBe(926);
    });

    it('handles low speed', () => {
      const kt = 50;
      const kmh = Math.round(kt * KT_TO_KMH);
      expect(kmh).toBe(93);
    });
  });

  describe('vertical speed', () => {
    it('converts fpm to m/s', () => {
      const fpm = 1000;
      const ms = fpm * FPM_TO_MS;
      expect(ms).toBeCloseTo(5.08, 1);
    });

    it('handles descent', () => {
      const fpm = -2000;
      const ms = fpm * FPM_TO_MS;
      expect(ms).toBeCloseTo(-10.16, 1);
    });
  });

  describe('distance', () => {
    it('converts km to nautical miles', () => {
      const km = 100;
      const nm = km * KM_TO_NM;
      expect(nm).toBeCloseTo(53.9957, 2);
    });

    it('handles short distance', () => {
      const km = 10;
      const nm = km * KM_TO_NM;
      expect(nm).toBeCloseTo(5.39957, 2);
    });
  });

  describe('range display', () => {
    it('formats metric range', () => {
      const rangeKm = 30;
      const display = `${rangeKm} km`;
      expect(display).toBe('30 km');
    });

    it('formats aviation range in nm', () => {
      const rangeKm = 30;
      const rangeNm = (rangeKm * KM_TO_NM).toFixed(0);
      const display = `${rangeNm} nm`;
      expect(display).toBe('16 nm');
    });
  });
});
