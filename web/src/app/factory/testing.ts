import { FactoryLevel } from './factory-models';

/** A small level for unit tests: cotton -> carder -> market, plus an empty slot. */
export function tinyLevel(): FactoryLevel {
  return {
    format: 'processforge-factory/1',
    id: 'tiny',
    name: 'Tiny',
    description: 'Card cotton into stuffing.',
    hours: 6,
    belt_capacity: 10,
    buffer_batches: 2,
    budget: null,
    resources: [
      { id: 'cotton', name: 'Cotton', kind: 'raw' },
      { id: 'stuffing', name: 'Stuffing', kind: 'product' },
    ],
    suppliers: [
      { id: 'cotton-supply', resource: 'cotton', max_rate: 10, unit_cost: 40, x: 0, y: 0 },
    ],
    machine_types: [
      {
        id: 'carder',
        name: 'Carder',
        cost: 1000,
        recipes: [
          {
            id: 'stuffing',
            name: 'Card stuffing',
            inputs: { cotton: 3 },
            outputs: { stuffing: 4 },
            hours: 1,
          },
        ],
      },
    ],
    market: {
      x: 6,
      y: 0,
      listings: [{ resource: 'stuffing', price: 100, demand: 4, overflow_price: 50 }],
    },
    slots: [
      { id: 's1', x: 3, y: 0 },
      { id: 's2', x: 3, y: 2 },
    ],
    objectives: [{ kind: 'profit', at_least: 100, text: 'Earn $1' }],
    reference_designs: [
      {
        name: 'Initial',
        initial: true,
        profit: -100,
        design: {
          orders: { 'cotton-supply': 3 },
          machines: [{ slot: 's1', type: 'carder', recipe: 'stuffing' }],
          belts: [
            { from: 'cotton-supply', to: 's1', resource: 'cotton', weight: 1 },
            { from: 's1', to: 'market', resource: 'stuffing', weight: 1 },
          ],
        },
      },
    ],
  };
}
