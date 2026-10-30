import {
  lucideBalloon,
  lucideBot,
  lucideCar,
  lucideCloud,
  lucideCog,
  lucideDisc3,
  lucideFan,
  lucideFrame,
  lucideGauge,
  lucideGuitar,
  lucideMilk,
  lucideMusic,
  lucidePackage,
  lucidePaintbrush,
  lucidePalette,
  lucidePin,
  lucideRabbit,
  lucideRecycle,
  lucideRuler,
  lucideShirt,
  lucideShoppingCart,
  lucideSpool,
  lucideSprout,
  lucideTreePine,
  lucideWind,
} from '@ng-icons/lucide';

/** Icon (a Lucide SVG) and belt-item color per toy-catalog resource; unknown ids get a box. */
const ICONS: Record<string, string> = {
  cotton: lucideSprout,
  string: lucideSpool,
  plastic: lucideMilk,
  rubber: lucideBalloon,
  paint: lucidePaintbrush,
  wood: lucideTreePine,
  fabric: lucideShirt,
  stuffing: lucideCloud,
  'guitar-body': lucideMusic,
  'painted-body': lucidePalette,
  offcut: lucideRecycle,
  dowel: lucideRuler,
  pegs: lucidePin,
  wheels: lucideDisc3,
  chassis: lucideFrame,
  gears: lucideCog,
  motor: lucideFan,
  'stuffed-animal': lucideRabbit,
  'toy-guitar': lucideGuitar,
  'toy-car': lucideCar,
  kite: lucideWind,
  robot: lucideBot,
  racer: lucideGauge,
};

const ITEM_COLORS: Record<string, string> = {
  cotton: '#a7d98b',
  string: '#f4a3c0',
  plastic: '#7cc6f2',
  rubber: '#f2a65a',
  paint: '#ff7ac8',
  wood: '#c79a6b',
  fabric: '#c9a7f2',
  stuffing: '#f0f0f0',
  'guitar-body': '#e0b07a',
  'painted-body': '#e8556d',
  offcut: '#9aa48a',
  dowel: '#d8c08c',
  pegs: '#f26b6b',
  wheels: '#9aa0aa',
  chassis: '#b0b8c8',
  gears: '#d4d9e0',
  motor: '#5a8dee',
  'stuffed-animal': '#f2c46b',
  'toy-guitar': '#ff8c5a',
  'toy-car': '#ff6b6b',
  kite: '#6be0c8',
  robot: '#c0c8d8',
  racer: '#ff3b3b',
};

/** The market's icon. */
export const MARKET_ICON = lucideShoppingCart;

export function iconFor(resource: string): string {
  return ICONS[resource] ?? lucidePackage;
}

export function itemColor(resource: string): string {
  return ITEM_COLORS[resource] ?? '#e8c33d';
}
