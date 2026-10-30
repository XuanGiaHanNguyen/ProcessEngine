import * as ops from './design-ops';
import { Design } from './factory-models';
import { tinyLevel } from './testing';

const empty: Design = { orders: {}, machines: [], belts: [] };

describe('design ops', () => {
  const level = tinyLevel();

  it('places a machine and belts it to everything it can use', () => {
    const d = ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', true);
    expect(d.machines).toEqual([{ slot: 's1', type: 'carder', recipe: 'stuffing' }]);
    expect(d.belts).toEqual([
      { from: 'cotton-supply', to: 's1', resource: 'cotton', weight: 1 },
      { from: 's1', to: 'market', resource: 'stuffing', weight: 1 },
    ]);
    expect(ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', false).belts).toEqual([]);
  });

  it('connects compatible nodes and explains incompatible ones', () => {
    const d = ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', false);
    const ok = ops.connect(level, d, 'cotton-supply', 's1');
    expect('design' in ok && ok.design.belts).toEqual([
      { from: 'cotton-supply', to: 's1', resource: 'cotton', weight: 1 },
    ]);
    const again = 'design' in ok ? ops.connect(level, ok.design, 'cotton-supply', 's1') : ok;
    expect(again).toEqual({ error: 'That belt already exists.' });
    expect(ops.connect(level, d, 'cotton-supply', 'market')).toEqual({
      error: 'Nothing to carry: it makes Cotton, but the target takes Stuffing.',
    });
    expect(ops.connect(level, d, 'market', 's1')).toEqual({
      error: 'Belts start at a supplier or a machine.',
    });
    expect(ops.connect(level, d, 's1', 's2')).toEqual({
      error: 'Belts end at a machine or the market.',
    });
  });

  it('removing a machine removes its belts', () => {
    const d = ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', true);
    expect(ops.removeMachine(d, 's1')).toEqual(empty);
  });

  it('clamps weights and computes build cost', () => {
    const d = ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', true);
    expect(ops.setWeight(d, 0, 99).belts[0].weight).toBe(10);
    expect(ops.setWeight(d, 0, 0).belts[0].weight).toBe(1);
    expect(ops.buildCost(level, d)).toBe(1000);
    expect(ops.setOrder(d, 'cotton-supply', 4).orders).toEqual({ 'cotton-supply': 4 });
  });

  it('clones deeply', () => {
    const d = ops.placeMachine(level, empty, 's1', 'carder', 'stuffing', true);
    const c = ops.cloneDesign(d);
    c.belts[0].weight = 5;
    expect(d.belts[0].weight).toBe(1);
  });
});
