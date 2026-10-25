import { Belt, Design, FactoryLevel, PlacedMachine, Recipe } from './factory-models';

/**
 * Pure helpers for editing a design. They only decide what is *possible* to draw or offer
 * in the editor; the server's /api/factory/validate stays the judge of validity.
 */

export function recipeOf(level: FactoryLevel, m: PlacedMachine): Recipe | undefined {
  return level.machine_types.find((t) => t.id === m.type)?.recipes.find((r) => r.id === m.recipe);
}

export function machineAt(design: Design, slot: string): PlacedMachine | undefined {
  return design.machines.find((m) => m.slot === slot);
}

/** Resources a node can send: a supplier's resource or a machine recipe's outputs. */
export function emits(level: FactoryLevel, design: Design, node: string): string[] {
  const supplier = level.suppliers.find((s) => s.id === node);
  if (supplier) return [supplier.resource];
  const m = machineAt(design, node);
  const r = m && recipeOf(level, m);
  return r ? Object.keys(r.outputs) : [];
}

/** Resources a node takes: a machine recipe's inputs, or what the market buys. */
export function accepts(level: FactoryLevel, design: Design, node: string): string[] {
  if (node === 'market') return level.market.listings.map((l) => l.resource);
  const m = machineAt(design, node);
  const r = m && recipeOf(level, m);
  return r ? Object.keys(r.inputs) : [];
}

function sameBelt(a: Belt, b: Belt): boolean {
  return a.from === b.from && a.to === b.to && a.resource === b.resource;
}

/** Adds belts that don't exist yet. */
function withBelts(design: Design, belts: Belt[]): Design {
  const add = belts.filter((b) => !design.belts.some((x) => sameBelt(x, b)));
  return { ...design, belts: [...design.belts, ...add] };
}

/**
 * Belts from `from` to `to` for every resource one makes and the other takes, or a reason
 * why there are none.
 */
export function connect(
  level: FactoryLevel,
  design: Design,
  from: string,
  to: string,
): { design: Design; added: number } | { error: string } {
  if (from === to) return { error: 'A belt needs two different ends.' };
  const out = emits(level, design, from);
  const inn = accepts(level, design, to);
  const shared = out.filter((r) => inn.includes(r));
  if (!out.length) return { error: 'Belts start at a supplier or a machine.' };
  if (!inn.length) return { error: 'Belts end at a machine or the market.' };
  if (!shared.length) {
    const name = (id: string) => level.resources.find((r) => r.id === id)?.name ?? id;
    return {
      error: `Nothing to carry: it makes ${out.map(name).join(', ')}, but the target takes ${inn.map(name).join(', ')}.`,
    };
  }
  const belts = shared.map((resource) => ({ from, to, resource, weight: 1 }));
  const next = withBelts(design, belts);
  const added = next.belts.length - design.belts.length;
  if (!added) return { error: 'That belt already exists.' };
  return { design: next, added };
}

/** Every node a placed machine could be belted to, in both directions. */
function autoBelts(level: FactoryLevel, design: Design, slot: string): Belt[] {
  const belts: Belt[] = [];
  const nodes = [
    ...level.suppliers.map((s) => s.id),
    ...design.machines.map((m) => m.slot),
    'market',
  ];
  for (const other of nodes) {
    if (other === slot) continue;
    for (const [from, to] of [
      [other, slot],
      [slot, other],
    ]) {
      const shared = emits(level, design, from).filter((r) =>
        accepts(level, design, to).includes(r),
      );
      for (const resource of shared) belts.push({ from, to, resource, weight: 1 });
    }
  }
  return belts;
}

/** Builds a machine in an empty slot, optionally belting it to everything it can use. */
export function placeMachine(
  level: FactoryLevel,
  design: Design,
  slot: string,
  type: string,
  recipe: string,
  autoConnect: boolean,
): Design {
  const placed = removeMachine(design, slot);
  const next: Design = { ...placed, machines: [...placed.machines, { slot, type, recipe }] };
  return autoConnect ? withBelts(next, autoBelts(level, next, slot)) : next;
}

/** Removes a machine and every belt touching its slot. */
export function removeMachine(design: Design, slot: string): Design {
  return {
    ...design,
    machines: design.machines.filter((m) => m.slot !== slot),
    belts: design.belts.filter((b) => b.from !== slot && b.to !== slot),
  };
}

/** Switches a machine's recipe and drops belts that no longer fit it. */
export function changeRecipe(
  level: FactoryLevel,
  design: Design,
  slot: string,
  recipe: string,
): Design {
  const machines = design.machines.map((m) => (m.slot === slot ? { ...m, recipe } : m));
  const next = { ...design, machines };
  const belts = next.belts.filter(
    (b) =>
      (b.from !== slot || emits(level, next, slot).includes(b.resource)) &&
      (b.to !== slot || accepts(level, next, slot).includes(b.resource)),
  );
  return { ...next, belts };
}

export function setOrder(design: Design, supplier: string, rate: number): Design {
  return { ...design, orders: { ...design.orders, [supplier]: rate } };
}

export function removeBelt(design: Design, index: number): Design {
  return { ...design, belts: design.belts.filter((_, i) => i !== index) };
}

export function setWeight(design: Design, index: number, weight: number): Design {
  return {
    ...design,
    belts: design.belts.map((b, i) =>
      i === index ? { ...b, weight: Math.min(10, Math.max(1, weight)) } : b,
    ),
  };
}

/** Sum of the build costs of the design's machines, in cents. */
export function buildCost(level: FactoryLevel, design: Design): number {
  return design.machines.reduce(
    (sum, m) => sum + (level.machine_types.find((t) => t.id === m.type)?.cost ?? 0),
    0,
  );
}

/** A deep copy, so drafts and tests never share arrays. */
export function cloneDesign(d: Design): Design {
  return {
    orders: { ...d.orders },
    machines: d.machines.map((m) => ({ ...m })),
    belts: d.belts.map((b) => ({ ...b })),
  };
}
