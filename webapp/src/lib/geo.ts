// Pure geo math — 1:1 port of src/geo.h (firmware).

export const deg2rad = (d: number): number => (d * Math.PI) / 180;
export const rad2deg = (r: number): number => (r * 180) / Math.PI;

// Great-circle distance in kilometers.
export function haversineKm(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const R = 6371.0088;
  const dlat = deg2rad(lat2 - lat1);
  const dlon = deg2rad(lon2 - lon1);
  const a =
    Math.sin(dlat / 2) * Math.sin(dlat / 2) +
    Math.cos(deg2rad(lat1)) * Math.cos(deg2rad(lat2)) * Math.sin(dlon / 2) * Math.sin(dlon / 2);
  return 2 * R * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

// Initial bearing from point 1 to point 2, degrees clockwise from true north [0,360).
export function bearingDeg(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const y = Math.sin(deg2rad(lon2 - lon1)) * Math.cos(deg2rad(lat2));
  const x =
    Math.cos(deg2rad(lat1)) * Math.sin(deg2rad(lat2)) -
    Math.sin(deg2rad(lat1)) * Math.cos(deg2rad(lat2)) * Math.cos(deg2rad(lon2 - lon1));
  const b = rad2deg(Math.atan2(y, x));
  return ((b % 360) + 360) % 360;
}

export const kmToNm = (km: number): number => km * 0.539957;

// Project a target to screen pixels.
// distKm/bearing: from home to target. rangeKm: outer-ring distance.
// rotationDeg: 0 for north-up. Beyond-range targets clamp to the rim (inRange=false).
export interface ScreenPoint { x: number; y: number; inRange: boolean; }
export function projectToScreen(
  distKm: number, bearing: number, rangeKm: number,
  cx: number, cy: number, rOuterPx: number, rotationDeg = 0,
): ScreenPoint {
  let rPx = (distKm / rangeKm) * rOuterPx;
  const inRange = distKm <= rangeKm;
  if (!inRange) rPx = rOuterPx; // clamp to rim
  const ang = deg2rad(bearing - rotationDeg);
  return {
    x: cx + rPx * Math.sin(ang),
    y: cy - rPx * Math.cos(ang), // screen Y grows downward; north is up
    inRange,
  };
}
