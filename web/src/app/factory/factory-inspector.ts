import { NgTemplateOutlet } from '@angular/common';
import { ChangeDetectionStrategy, Component, computed, inject, signal } from '@angular/core';

import * as ops from './design-ops';
import { Belt, MachineType, Recipe, money } from './factory-models';
import { FactoryStore } from './factory-store';
import { iconFor } from './icons';

interface BeltRow {
  index: number;
  belt: Belt;
  label: string;
}

/**
 * The right-hand panel: edits whatever is selected on the floor (supplier order, machine
 * recipe, belt weight), offers machines for an empty slot, and shows the objectives when
 * nothing is selected.
 */
@Component({
  selector: 'app-factory-inspector',
  imports: [NgTemplateOutlet],
  templateUrl: './factory-inspector.html',
  styleUrl: './factory-inspector.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class FactoryInspector {
  protected readonly store = inject(FactoryStore);
  protected readonly money = money;
  protected readonly iconFor = iconFor;
  /** Belt new machines to everything they can use. */
  protected readonly autoConnect = signal(true);

  private readonly level = this.store.level;
  private readonly draft = this.store.draft;

  protected readonly node = computed(() => {
    const sel = this.store.selection();
    return sel?.kind === 'node' ? sel.id : null;
  });

  protected readonly supplier = computed(
    () => this.level()?.suppliers.find((s) => s.id === this.node()) ?? null,
  );
  protected readonly order = computed(() => {
    const s = this.supplier();
    return s ? (this.draft()?.orders[s.id] ?? 0) : 0;
  });

  protected readonly machine = computed(() => {
    const id = this.node();
    const draft = this.draft();
    const level = this.level();
    if (!id || !draft || !level) return null;
    const placed = ops.machineAt(draft, id);
    const type = placed && level.machine_types.find((t) => t.id === placed.type);
    const recipe = placed && ops.recipeOf(level, placed);
    return placed && type && recipe ? { slot: id, type, recipe } : null;
  });

  protected readonly emptySlot = computed(() => {
    const id = this.node();
    return id && this.level()?.slots.some((s) => s.id === id) && !this.machine() ? id : null;
  });

  protected readonly belt = computed(() => {
    const sel = this.store.selection();
    const draft = this.draft();
    if (sel?.kind !== 'belt' || !draft) return null;
    const b = draft.belts[sel.index];
    return b ? { index: sel.index, belt: b, label: this.beltLabel(b) } : null;
  });

  /** Belts touching the selected node, for its belt list. */
  protected readonly nodeBelts = computed<{ incoming: BeltRow[]; outgoing: BeltRow[] }>(() => {
    const id = this.node();
    const rows = (this.draft()?.belts ?? []).map((belt, index) => ({
      index,
      belt,
      label: this.beltLabel(belt),
    }));
    return {
      incoming: rows.filter((r) => r.belt.to === id),
      outgoing: rows.filter((r) => r.belt.from === id),
    };
  });

  /** How the selected belt's source splits a resource between its belts. */
  protected readonly split = computed(() => {
    const b = this.belt();
    if (!b) return null;
    const siblings = (this.draft()?.belts ?? []).filter(
      (x) => x.from === b.belt.from && x.resource === b.belt.resource,
    );
    const total = siblings.reduce((sum, x) => sum + (x.weight ?? 1), 0);
    return siblings.length > 1 ? Math.round(((b.belt.weight ?? 1) / total) * 100) : null;
  });

  protected readonly over = computed(() => {
    const budget = this.level()?.budget;
    return budget != null ? budget - this.store.buildCost() : null;
  });

  protected name(node: string): string {
    if (node === 'market') return 'Market';
    const level = this.level();
    const supplier = level?.suppliers.find((s) => s.id === node);
    if (supplier) return `${this.resourceName(supplier.resource)} supplier`;
    const draft = this.draft();
    const m = draft && ops.machineAt(draft, node);
    const type = m && level?.machine_types.find((t) => t.id === m.type);
    return type ? `${type.name} (${node})` : node;
  }

  protected resourceName(id: string): string {
    return this.level()?.resources.find((r) => r.id === id)?.name ?? id;
  }

  protected entries(rec: Record<string, number>): { id: string; qty: number }[] {
    return Object.entries(rec).map(([id, qty]) => ({ id, qty }));
  }

  protected outputIcon(recipe: Recipe): string {
    return iconFor(Object.keys(recipe.outputs)[0] ?? '');
  }

  protected setOrder(value: string): void {
    const s = this.supplier();
    const n = Math.round(Number(value));
    if (!s || !Number.isFinite(n)) return;
    const rate = Math.min(s.max_rate, Math.max(0, n));
    if (rate === this.order()) return;
    this.store.edit((_, d) => ops.setOrder(d, s.id, rate));
  }

  protected build(type: MachineType, recipe: Recipe): void {
    const slot = this.emptySlot();
    if (!slot) return;
    const auto = this.autoConnect();
    this.store.edit((level, d) => ops.placeMachine(level, d, slot, type.id, recipe.id, auto));
  }

  protected changeRecipe(recipe: string): void {
    const m = this.machine();
    if (!m || m.recipe.id === recipe) return;
    this.store.edit((level, d) => ops.changeRecipe(level, d, m.slot, recipe));
  }

  protected removeMachine(): void {
    const m = this.machine();
    if (!m) return;
    this.store.edit((_, d) => ops.removeMachine(d, m.slot));
  }

  protected setWeight(index: number, weight: number): void {
    this.store.edit((_, d) => ops.setWeight(d, index, weight));
  }

  protected removeBelt(index: number): void {
    this.store.edit((_, d) => ops.removeBelt(d, index));
    this.store.selection.set(null);
  }

  protected selectBelt(index: number): void {
    this.store.selection.set({ kind: 'belt', index });
  }

  private beltLabel(b: Belt): string {
    return `${iconFor(b.resource)} ${this.resourceName(b.resource)}: ${this.name(b.from)} → ${this.name(b.to)}`;
  }
}
