/** Emoji icon and belt-item color per toy-catalog resource; unknown ids get a generic box. */
const ICONS: Record<string, string> = {
  cotton: '🌿',
  string: '🧵',
  plastic: '🧴',
  rubber: '🎈',
  wood: '🪵',
  fabric: '🧶',
  stuffing: '☁️',
  'guitar-body': '🪕',
  offcut: '♻️',
  dowel: '🥢',
  pegs: '📌',
  wheels: '🛞',
  chassis: '🔩',
  'stuffed-animal': '🧸',
  'toy-guitar': '🎸',
  'toy-car': '🚗',
  kite: '🪁',
};

const ITEM_COLORS: Record<string, string> = {
  cotton: '#a7d98b',
  string: '#f4a3c0',
  plastic: '#7cc6f2',
  rubber: '#f2a65a',
  wood: '#c79a6b',
  fabric: '#c9a7f2',
  stuffing: '#f0f0f0',
  'guitar-body': '#e0b07a',
  offcut: '#9aa48a',
  dowel: '#d8c08c',
  pegs: '#f26b6b',
  wheels: '#9aa0aa',
  chassis: '#b0b8c8',
  'stuffed-animal': '#f2c46b',
  'toy-guitar': '#ff8c5a',
  'toy-car': '#ff6b6b',
  kite: '#6be0c8',
};

export function iconFor(resource: string): string {
  return ICONS[resource] ?? '📦';
}

export function itemColor(resource: string): string {
  return ITEM_COLORS[resource] ?? '#e8c33d';
}
