import easy from '../../../../instances/factory/1-first-stitch.json';
import hard from '../../../../instances/factory/2-full-line.json';
import expert from '../../../../instances/factory/3-toy-empire.json';
import { FLOOR_3D, FloorBelt, layoutFloor } from './floor-layout';
import { FactoryLevel } from './factory-models';
import { money } from './factory-models';
import { tinyLevel } from './testing';

describe('layoutFloor', () => {
  const design = () => tinyLevel().reference_designs[0].design;

  it('lays out suppliers, built machines, the market and belts, but no empty slots', () => {
    const { nodes, belts } = layoutFloor(tinyLevel(), design(), null);
    expect(nodes.map((n) => n.kind)).toEqual(['supplier', 'machine', 'market']);
    expect(belts).toHaveLength(2);
    expect(nodes[0].badge).toBe('3/h');
    expect(nodes[1].title).toBe('Carder: Card stuffing');
    // Left to right: supplier, then machine, then market.
    expect(nodes[0].x).toBeLessThan(nodes[1].x);
    expect(nodes[1].x).toBeLessThan(nodes[2].x);
  });

  it('gives suppliers storage only when the level has a warehouse', () => {
    expect(layoutFloor(tinyLevel(), design(), null).nodes[0].storage).toBeUndefined();
    const stocked = layoutFloor({ ...tinyLevel(), storage: 50 }, design(), null).nodes[0];
    // Nothing is stored before a test.
    expect(stocked.storage).toEqual({ held: 0, cap: 50 });
  });

  it('follows the design, and skipping routing keeps the nodes the same', () => {
    expect(layoutFloor(tinyLevel(), { orders: {}, machines: [], belts: [] }, null).belts).toEqual(
      [],
    );
    const routed = layoutFloor(tinyLevel(), design(), null);
    const quick = layoutFloor(tinyLevel(), design(), null, { route: false });
    expect(quick.nodes).toEqual(routed.nodes);
    expect(quick.belts.map((b) => b.key)).toEqual(routed.belts.map((b) => b.key));
  });

  it('puts the market further away when asked', () => {
    const near = layoutFloor(tinyLevel(), design(), null).nodes[2].x;
    const far = layoutFloor(tinyLevel(), design(), null, { marketGap: 220 }).nodes[2].x;
    expect(far - near).toBe(150);
  });
});

/** Pairs of belts that run side by side closer than a 3D belt is wide (0.56 world units, 11
 * floor units), so they read as one belt. Belts fanning out of one port side may be a little
 * closer than the router's preferred spacing, but never this close. */
function sharedStretches(belts: FloorBelt[]): string[] {
  const segs = belts.map((b) =>
    b.points.slice(1).flatMap((q, i) => {
      const p = b.points[i];
      if (Math.abs(q[1] - p[1]) < 0.5)
        return Math.abs(q[0] - p[0]) < 0.5 ? [] : [{ h: true, at: q[1], a: p[0], b: q[0] }];
      return [{ h: false, at: q[0], a: p[1], b: q[1] }];
    }),
  );
  const span = (a: number, b: number, c: number, d: number) =>
    Math.min(Math.max(a, b), Math.max(c, d)) - Math.max(Math.min(a, b), Math.min(c, d));
  const out: string[] = [];
  for (let i = 0; i < belts.length; i++)
    for (let j = i + 1; j < belts.length; j++)
      if (
        segs[i].some((s) =>
          segs[j].some(
            (o) => s.h === o.h && Math.abs(s.at - o.at) < 12 && span(s.a, s.b, o.a, o.b) > 2,
          ),
        )
      )
        out.push(`${belts[i].key} / ${belts[j].key}`);
  return out;
}

describe('the 3D floor of each level', () => {
  for (const level of [easy, hard, expert] as unknown as FactoryLevel[])
    for (const ref of level.reference_designs)
      it(`${level.name}, ${ref.name}: no two belts share a stretch`, () => {
        const floor = layoutFloor(level, ref.design, null, FLOOR_3D);
        expect(sharedStretches(floor.belts)).toEqual([]);
      });
});

describe('money', () => {
  it('formats cents as dollars', () => {
    expect(money(1620)).toBe('$16.20');
    expect(money(-305)).toBe('-$3.05');
    expect(money(16580000)).toBe('$165,800');
    expect(money(-250000)).toBe('-$2,500');
    expect(money(0)).toBe('$0');
  });
});
