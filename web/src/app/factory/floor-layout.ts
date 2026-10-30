import { Belt, Design, FactoryLevel, HourReport, money } from './factory-models';
import {
  lucideCog,
  lucideHourglass,
  lucideOctagonX,
  lucidePackage,
  lucideTimer,
} from '@ng-icons/lucide';

import { iconFor } from './icons';

/**
 * The factory floor's layout: suppliers, machines in columns by stage and the market, with
 * each belt routed in its own lane, in floor coordinates. The 3D floor draws it.
 */

const total = (counts: Record<string, number>) => Object.values(counts).reduce((a, b) => a + b, 0);

type NodeKind = 'supplier' | 'machine' | 'market';

/** A drawable node: a supplier, a built machine, or the market. */
export interface FloorNode {
  id: string;
  kind: NodeKind;
  x: number;
  y: number;
  icon: string;
  title: string;
  badge: string;
  sub: string;
  /** Icons (SVG) shown before the badge and sub lines, if any. */
  badgeIcon?: string;
  subIcon?: string;
  active: boolean;
  /** Machine status during playback: busy, blocked or starved. */
  status?: string;
  /** Storage: for a machine, units held in its buffers and the batch being made, of what it
   * can hold; for a supplier, its stock, of the warehouse all suppliers share. */
  storage?: { held: number; cap: number };
  /** How many machines making the same thing this node stands for. */
  count?: number;
}

/** The scenery on the floor: feeds from the left edge to the suppliers. */
export interface Scene {
  feeds: { key: string; y: number; x2: number; active: boolean }[];
}

/** A drawable belt between ports on two nodes. */
export interface FloorBelt {
  key: string;
  /** The first of the design's belts this one draws. */
  index: number;
  /** All the design's belts it draws: belts between machines shown as one are merged. */
  indices: number[];
  from: string;
  to: string;
  resource: string;
  weight: number;
  /** Units carried this hour during playback, else null. */
  moved: number | null;
  /** The belt's corner points, from source to target. */
  points: [number, number][];
  /** +1 when the belt enters its target from the left, -1 from the right. */
  entry: 1 | -1;
  source: [number, number];
  target: [number, number];
  active: boolean;
}

/** A belt while it is being routed: its nodes, side, ports and the gap it turns in. */
interface Route {
  belt: DrawnBelt;
  from: FloorNode;
  to: FloorNode;
  entry: 1 | -1;
  source: [number, number];
  target: [number, number];
}

/** A design belt as drawn: its ends mapped to the nodes shown, merged with its twins. */
type DrawnBelt = Belt & { indices: number[] };

/** One end of a routed belt on a side of a node. */
interface PortUse {
  route: Route;
  end: 'source' | 'target';
  other: FloorNode;
}

/** What the floor highlights: a node by id, or a belt by its index in the design. */
export type FloorSelection = { kind: 'node'; id: string } | { kind: 'belt'; index: number } | null;

export const R = 30;
/** Vertical distance between machines in a column. */
const ROW = 170;
/** Radius of a belt's turns: large enough to read as a gentle curve. */
export const CORNER = 30;
/** Vertical spacing between belts leaving or entering the same side of a node. */
const PORT_GAP = 22;
/** Horizontal spacing between the vertical runs of belts that turn in the same gap. */
const LANE_GAP = 24;
/** Belts closer than this, side by side, look like one belt. */
const SEP = 18;

/** How to lay out the floor. */
export interface FloorOptions {
  /** Spacing between belts turning side by side in a gap between columns. */
  laneGap?: number;
  /** Distance from the last column of machines to the market's wall. */
  marketGap?: number;
  /** Routing is the slow part and the hour never changes it, so `false` skips it for a
   * caller that only needs the hour's state: its belts run straight from port to port. */
  route?: boolean;
}

/** The 3D floor's layout: belts that meet at a machine turn in lanes wider apart than on
 * the 2D floor, and the finished toys' machines stand back from the market. */
export const FLOOR_3D: FloorOptions = { laneGap: 40, marketGap: 160 };

/** The floor laid out: its nodes and routed belts, which shown machine each slot is part of,
 * its size and its scenery. */
export interface FloorLayout {
  nodes: FloorNode[];
  belts: FloorBelt[];
  groupOf: Map<string, string>;
  bounds: { w: number; h: number };
  scene: Scene;
}

/**
 * Lays out the floor in floor coordinates: suppliers, machines in columns by stage and the
 * market, with each belt routed in its own lane.
 */
export function layoutFloor(
  level: FactoryLevel,
  design: Design | null,
  report: HourReport | null,
  options: FloorOptions = {},
): FloorLayout {
  const { laneGap = LANE_GAP, marketGap = 70, route = true } = options;
  const nodes: FloorNode[] = [];
  const orders = design?.orders ?? {};
  const machines = design?.machines ?? [];
  const hrs = (n: number) => `${n} hr${n > 1 ? 's' : ''}`;
  // A supplier's stock, and how much of its order came in when the warehouse cut it.
  const supplied = (id: string, rate: number) => {
    const got = report?.delivered?.[id] ?? rate;
    return `📦 ${report?.stock[id] ?? 0}${got < rate ? ` · ${got}/${rate} in` : ''}`;
  };

  // Columns by stage: raw ingredients, then each layer of parts made from them, and the
  // finished products last. A resource's stage is one more than its latest input's.
  const stage = new Map<string, number>();
  for (const r of level.resources) if (r.kind === 'raw') stage.set(r.id, 0);
  const recipes = level.machine_types.flatMap((t) => t.recipes);
  for (let changed = true; changed;) {
    changed = false;
    for (const rc of recipes) {
      const ins = Object.keys(rc.inputs).map((i) => stage.get(i));
      if (ins.some((v) => v === undefined)) continue;
      const st = 1 + Math.max(0, ...(ins as number[]));
      for (const o of Object.keys(rc.outputs))
        if (st < (stage.get(o) ?? Infinity)) {
          stage.set(o, st);
          changed = true;
        }
    }
  }
  const last = Math.max(1, ...stage.values());
  const kindOf = new Map(level.resources.map((r) => [r.id, r.kind]));
  const stageOf = (outputs: Record<string, number>) =>
    Math.max(
      1,
      ...Object.keys(outputs).map((o) =>
        kindOf.get(o) === 'product' ? last : (stage.get(o) ?? 1),
      ),
    );

  const built = level.slots.flatMap((slot) => {
    const m = machines.find((pm) => pm.slot === slot.id);
    const type = m && level.machine_types.find((t) => t.id === m.type);
    const recipe = type?.recipes.find((r) => r.id === m?.recipe);
    return m && type && recipe ? [{ slot, m, type, recipe, col: stageOf(recipe.outputs) }] : [];
  });
  // Machines making the same thing show as one machine, ×n, under the first one's slot.
  const groups: { id: string; col: number; members: typeof built }[] = [];
  const groupOf = new Map<string, string>();
  for (const b of built) {
    const g = groups.find((x) => x.members[0].type === b.type && x.members[0].recipe === b.recipe);
    if (g) g.members.push(b);
    else groups.push({ id: b.slot.id, col: b.col, members: [b] });
    groupOf.set(b.slot.id, g?.id ?? b.slot.id);
  }
  const shown = (id: string) => groupOf.get(id) ?? id;
  const drawn: DrawnBelt[] = [];
  for (const [i, bl] of (design?.belts ?? []).entries()) {
    const [from, to] = [shown(bl.from), shown(bl.to)];
    const twin = drawn.find((d) => d.from === from && d.to === to && d.resource === bl.resource);
    if (twin) twin.indices.push(i);
    else if (from !== to) drawn.push({ ...bl, from, to, indices: [i] });
  }
  const colIds = [...new Set(groups.map((g) => g.col))].sort((a, b) => a - b);

  // Rows: suppliers in level order; machines ordered by where their inputs come from, so
  // belts run as straight as they can.
  const rowOf = new Map<string, number>();
  level.suppliers.forEach((s, i) => rowOf.set(s.id, i));
  const columnsOf: { id: string }[][] = [level.suppliers];
  for (const c of colIds) {
    const here = groups.filter((g) => g.col === c);
    const pull = (id: string) => {
      const ins = drawn
        .filter((bl) => bl.to === id && rowOf.has(bl.from))
        .map((bl) => rowOf.get(bl.from)!);
      return ins.length ? ins.reduce((a, b) => a + b, 0) / ins.length : Infinity;
    };
    here.sort((a, b) => pull(a.id) - pull(b.id));
    here.forEach((g, i) => rowOf.set(g.id, i));
    columnsOf.push(here.map((g) => ({ id: g.id })));
  }
  const tallest = Math.max(1, ...columnsOf.map((c) => c.length));
  const top = 50;
  const yAt = (i: number, n: number) => top + ((tallest - n) / 2 + i) * ROW;
  const floorBottom = top + (tallest - 1) * ROW;

  // Column x: wider gaps where more belts need their own lane.
  const colOf = new Map<string, number>();
  columnsOf.forEach((c, i) => c.forEach((n) => colOf.set(n.id, i)));
  colOf.set('market', columnsOf.length);
  // A belt needs a lane only where it turns. One running straight on to the next column
  // needs none; one that changes rows turns once, so it takes a lane in the least used gap it
  // passes; one running back to the left turns after its source and again before its target.
  const yOf = (id: string) =>
    id === 'market' ? null : yAt(rowOf.get(id)!, columnsOf[colOf.get(id)!].length);
  const lanes = columnsOf.map(() => 0);
  for (const bl of drawn) {
    const [a, b] = [colOf.get(bl.from) ?? 0, colOf.get(bl.to) ?? 0];
    if (b <= a) {
      lanes[a] += 1;
      lanes[Math.max(0, b - 1)] += 1;
    } else if (b > a + 1 || (bl.to !== 'market' && yOf(bl.from) !== yOf(bl.to))) {
      let g = a;
      for (let i = a + 1; i < b && i < lanes.length; i++) if (lanes[i] < lanes[g]) g = i;
      lanes[g] += 1;
    }
  }
  const xs: number[] = [R + 60];
  for (let i = 1; i <= columnsOf.length; i++)
    xs.push(xs[i - 1] + Math.max(260, 2 * R + 2 * CORNER + 60 + laneGap * lanes[i - 1]));
  // Belts into the market end here on the floor and run on off the right edge.
  const dock = xs[columnsOf.length - 1] + R + marketGap;

  for (const s of level.suppliers) {
    const rate = orders[s.id] ?? 0;
    nodes.push({
      id: s.id,
      kind: 'supplier',
      x: xs[0],
      y: yAt(rowOf.get(s.id)!, level.suppliers.length),
      icon: iconFor(s.resource),
      title: level.resources.find((r) => r.id === s.resource)?.name ?? s.resource,
      badge: `${rate}/h`,
      sub: report ? supplied(s.id, rate) : `×${money(s.unit_cost)}`,
      active: rate > 0,
      // With a warehouse, its ring shows its stock of it, and it turns red when the
      // warehouse was too full for its whole order.
      ...(level.storage
        ? {
            storage: { held: report?.stock[s.id] ?? 0, cap: level.storage },
            status: (report?.delivered?.[s.id] ?? rate) < rate ? ('blocked' as const) : undefined,
          }
        : {}),
    });
  }
  for (const { id, members } of groups) {
    const { type, recipe } = members[0];
    const n = members.length;
    const col = colOf.get(id)!;
    const hours = report
      ? members.map((b) => report.machines[machines.indexOf(b.m)]).filter((h) => !!h)
      : [];
    // Storage: units held in the input and output buffers and in the batch being made,
    // of what they can hold.
    const batch = total(recipe.inputs);
    const cap = n * (level.buffer_batches * (batch + total(recipe.outputs)) + batch);
    const held = hours.reduce(
      (t, h) => t + total(h.inputs) + total(h.outputs) + (h.status === 'busy' ? batch : 0),
      0,
    );
    const busy = hours.filter((h) => h.status === 'busy');
    const status = busy.length
      ? 'busy'
      : hours.some((h) => h.status === 'blocked')
        ? 'blocked'
        : hours.length
          ? 'starved'
          : undefined;
    // Making: how much a batch (of all of them) makes and how long it takes, or where the
    // batch is.
    const lead = busy[0];
    const sub = !report
      ? `${n * total(recipe.outputs)} per ${hrs(recipe.hours)}`
      : lead
        ? `${lead.progress} of ${hrs(lead.total)}${n > 1 && busy.length < n ? ` · ${busy.length}/${n} on` : ''}`
        : status === 'blocked'
          ? 'Blocked'
          : 'Waiting';
    const subIcon = !report
      ? lucideCog
      : lead
        ? lucideTimer
        : status === 'blocked'
          ? lucideOctagonX
          : lucideHourglass;
    nodes.push({
      id,
      kind: 'machine',
      x: xs[col],
      y: yAt(rowOf.get(id)!, columnsOf[col].length),
      icon: iconFor(Object.keys(recipe.outputs)[0]),
      title: `${type.name}: ${recipe.name}${n > 1 ? ` ×${n}` : ''}`,
      badge: `${held}/${cap}`,
      badgeIcon: lucidePackage,
      sub,
      subIcon,
      active: report ? status === 'busy' : true,
      status,
      storage: { held, cap },
      count: n,
    });
  }

  nodes.push({
    id: 'market',
    kind: 'market',
    x: dock,
    y: (top + floorBottom) / 2,
    icon: '',
    title: 'Market',
    badge: report ? `+${money(report.revenue)}` : '',
    sub: report ? `${total(report.sold)} sold` : '',
    active: report ? report.revenue > 0 : true,
  });
  const bounds = { w: dock + 20, h: floorBottom + R + 80 };
  const scene: Scene = {
    feeds: level.suppliers.map((s) => {
      const n = nodes.find((nd) => nd.id === s.id)!;
      return { key: s.id, y: n.y, x2: n.x - R, active: n.active };
    }),
  };

  const byId = new Map(nodes.map((n) => [n.id, n]));
  const columns = [...new Set(nodes.map((n) => n.x))].sort((a, b) => a - b);

  // 1. Which side of each node a belt leaves or enters. Belts leave from the right; a
  // target further right is entered from its left, anything else from its right.
  const routes: Route[] = [];
  for (const b of drawn) {
    const from = byId.get(b.from);
    const to = byId.get(b.to);
    if (!from || !to) continue;
    const entry = to.x > from.x + R ? 1 : -1;
    routes.push({
      belt: b,
      from,
      to,
      entry,
      source: [0, 0],
      target: [0, 0],
    });
  }

  // 2. Ports: every belt has its own port. Belts sharing a side of a node fan out, ordered
  // by where their other end is, so they leave without crossing.
  const sides = new Map<string, PortUse[]>();
  const addSide = (key: string, item: PortUse) => sides.set(key, [...(sides.get(key) ?? []), item]);
  for (const r of routes) {
    addSide(`${r.from.id}|R`, { route: r, end: 'source', other: r.to });
    addSide(`${r.to.id}|${r.entry > 0 ? 'L' : 'R'}`, { route: r, end: 'target', other: r.from });
  }
  const marketSide = sides.get('market|L') ?? [];
  sides.delete('market|L');
  for (const [key, items] of sides) {
    const [id, side] = key.split('|');
    const node = byId.get(id)!;
    items.sort((a, b) => a.other.y - b.other.y || a.other.x - b.other.x);
    const gap = Math.min(PORT_GAP, (1.7 * R) / Math.max(1, items.length - 1));
    items.forEach((item, i) => {
      const off = (i - (items.length - 1) / 2) * gap;
      const dx = Math.sqrt(R * R - off * off);
      item.route[item.end] = [node.x + (side === 'R' ? dx : -dx), node.y + off];
    });
  }
  // Belts into the market go straight across into its wall where they can, apart from
  // each other.
  const mk = byId.get('market');
  if (mk) {
    marketSide.sort((a, b) => a.route.source[1] - b.route.source[1]);
    let prev = -Infinity;
    for (const item of marketSide) {
      const y = Math.max(item.route.source[1], prev + PORT_GAP);
      item.route.target = [mk.x, y];
      prev = y;
    }
  }

  const chosen = new Map<Route, [number, number][]>();
  if (route) {
    // 3. Candidate paths for each belt, as corner points: straight across; or across, down
    // or up a lane in a gap between columns, and across into the target; or, for a belt that
    // skips columns, a detour along a corridor between rows so it never runs through the
    // machines it passes.
    type Pt = [number, number];
    const between = columns.slice(0, -1).map((c, i) => (c + columns[i + 1]) / 2);
    const lanesIn = (gap: number, most = Infinity) => {
      const col = columns.findIndex((c) => c > gap);
      const room = col > 0 ? (columns[col] - columns[col - 1]) / 2 - R - CORNER : 2 * laneGap;
      const out: number[] = [];
      for (let k = 0; k * laneGap <= room && out.length < most; k++) {
        out.push(gap + k * laneGap);
        if (k) out.push(gap - k * laneGap);
      }
      return out;
    };
    const span = (a: number, b: number, c: number, d: number) =>
      Math.min(Math.max(a, b), Math.max(c, d)) - Math.max(Math.min(a, b), Math.min(c, d));
    const blocks = nodes
      .filter((n) => n.kind !== 'market')
      .flatMap((n) => [
        {
          node: n,
          x0: n.x - R - 6,
          x1: n.x + R + 6,
          y0: n.y - R - 6,
          y1: n.y + R + 6,
          w: 40,
        },
        ...(n.badge
          ? [
              {
                node: n,
                x0: n.x - 40,
                x1: n.x + 40,
                y0: n.y + R + 4,
                y1: n.y + R + 48,
                w: 12,
              },
            ]
          : []),
      ]);
    const ys = nodes.filter((n) => n.kind !== 'market').map((n) => n.y);
    const corridors: number[] = [];
    for (let y = Math.min(...ys) - ROW / 2; y <= Math.max(...ys) + ROW / 2; y += 12)
      corridors.push(y);
    const clear = (y: number, xa: number, xb: number) =>
      !blocks.some((b) => y > b.y0 - 8 && y < b.y1 + 8 && span(xa, xb, b.x0, b.x1) > 0);
    const options = new Map<Route, Pt[][]>();
    for (const r of routes) {
      const [x1, y1] = r.source;
      const [x2, y2] = r.target;
      const paths: Pt[][] = [];
      if (Math.abs(y2 - y1) < 1 && x2 > x1) paths.push([r.source, r.target]);
      const right = columns.find((c) => c > r.from.x);
      let gaps = r.entry > 0 && x2 > x1 ? between.filter((g) => g > x1 && g < x2) : [];
      if (!gaps.length)
        gaps = [
          r.entry > 0 && x2 > x1
            ? (x1 + x2) / 2
            : right !== undefined
              ? (r.from.x + right) / 2
              : x1 + CORNER + 16,
        ];
      for (const g of gaps)
        for (const x of lanesIn(g)) paths.push([r.source, [x, y1], [x, y2], r.target]);
      if (gaps.length > 1) {
        const ga = gaps[0];
        const gb = gaps[gaps.length - 1];
        for (const y of corridors) {
          if (!clear(y, ga, gb)) continue;
          for (const xa of lanesIn(ga, 5))
            for (const xb of lanesIn(gb, 5))
              paths.push([r.source, [xa, y1], [xa, y], [xb, y], [xb, y2], r.target]);
        }
      }
      options.set(r, paths);
    }

    // 4. Pick each belt's path so that no belt runs on top of another, through a node or over
    // a badge. Belts may cross, but never share a stretch. Greedy passes over the belts, each
    // taking the path that scores best against the others' current paths; shorter paths with
    // fewer turns win ties.
    interface Seg {
      h: boolean;
      at: number;
      a: number;
      b: number;
    }
    const toSegs = (pts: Pt[]): Seg[] =>
      pts.slice(1).flatMap((q, i): Seg[] => {
        const p0 = pts[i];
        if (Math.abs(q[1] - p0[1]) < 0.5)
          return Math.abs(q[0] - p0[0]) < 0.5 ? [] : [{ h: true, at: q[1], a: p0[0], b: q[0] }];
        return [{ h: false, at: q[0], a: p0[1], b: q[1] }];
      });
    const shape = (pts: Pt[]) =>
      toSegs(pts).reduce((t, sg) => t + Math.abs(sg.b - sg.a), 0) * 0.01 + (pts.length - 2) * 1.5;
    const picked = new Map<Route, { pts: Pt[]; segs: Seg[] }>();
    const cost = (r: Route, segs: Seg[]) => {
      let c = 0;
      for (const sg of segs)
        for (const b of blocks) {
          if (b.node === r.from || b.node === r.to) continue;
          const [lo, hi, x0, x1] = sg.h ? [b.y0, b.y1, b.x0, b.x1] : [b.x0, b.x1, b.y0, b.y1];
          if (sg.at > lo && sg.at < hi && span(sg.a, sg.b, x0, x1) > 0) c += b.w;
        }
      for (const [q, o] of picked) {
        if (q === r) continue;
        for (const sg of segs)
          for (const og of o.segs) {
            if (sg.h === og.h) {
              if (Math.abs(sg.at - og.at) < SEP && span(sg.a, sg.b, og.a, og.b) > 2) c += 30;
            } else if (
              og.at > Math.min(sg.a, sg.b) &&
              og.at < Math.max(sg.a, sg.b) &&
              sg.at > Math.min(og.a, og.b) &&
              sg.at < Math.max(og.a, og.b)
            )
              c += 1;
          }
      }
      return c;
    };
    for (let pass = 0; pass < 4; pass++) {
      let changed = false;
      for (const r of routes) {
        const now = picked.get(r);
        let best = now;
        let bestCost = now ? cost(r, now.segs) + shape(now.pts) : Infinity;
        for (const pts of options.get(r)!) {
          const segs = toSegs(pts);
          const c = cost(r, segs) + shape(pts);
          if (c < bestCost - 1e-9) {
            best = { pts, segs };
            bestCost = c;
          }
        }
        if (best && best !== now) {
          picked.set(r, best);
          changed = true;
        }
      }
      if (!changed) break;
    }
    for (const [r, v] of picked) chosen.set(r, v.pts);
  }

  const belts: FloorBelt[] = routes.map((r) => ({
    key: `${r.belt.from}>${r.belt.to}:${r.belt.resource}`,
    index: r.belt.indices[0],
    indices: r.belt.indices,
    from: r.from.id,
    to: r.to.id,
    points: chosen.get(r) ?? [r.source, r.target],
    entry: r.entry,
    resource: r.belt.resource,
    weight: r.belt.weight ?? 1,
    moved: report ? r.belt.indices.reduce((t, i) => t + (report.belts[i] ?? 0), 0) : null,
    source: r.source,
    target: r.target,
    active: report ? r.belt.indices.some((i) => (report.belts[i] ?? 0) > 0) : r.from.active,
  }));
  return { nodes, belts, groupOf, bounds, scene };
}
