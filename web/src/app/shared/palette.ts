/**
 * Twelve hues spaced around the color wheel, ordered so neighbors differ most, all dark
 * enough for white labels (>= 4.5:1).
 */
const COLORS = [
  '#1d5fbf', // blue
  '#c2410c', // orange
  '#15803d', // green
  '#7e22ce', // purple
  '#be185d', // pink
  '#0e7490', // cyan
  '#92400e', // brown
  '#4d7c0f', // olive
  '#475569', // slate
  '#b91c1c', // red
  '#3730a3', // indigo
  '#a16207', // amber
];

export function colorFor(index: number): string {
  return COLORS[((index % COLORS.length) + COLORS.length) % COLORS.length];
}

/** Stable color per process id within an instance. */
export function processColors(processIds: string[]): Map<string, string> {
  return new Map(processIds.map((id, i) => [id, colorFor(i)]));
}
