/** Distinct, colorblind-friendly-ish colors (Okabe-Ito + extras), dark enough for white text. */
const COLORS = [
  '#0b6fa4', '#c2410c', '#15803d', '#7e22ce', '#b45309', '#0f766e',
  '#be185d', '#4d7c0f', '#1d4ed8', '#9f1239', '#475569', '#a16207',
];

export function colorFor(index: number): string {
  return COLORS[((index % COLORS.length) + COLORS.length) % COLORS.length];
}

/** Stable color per process id within an instance. */
export function processColors(processIds: string[]): Map<string, string> {
  return new Map(processIds.map((id, i) => [id, colorFor(i)]));
}
