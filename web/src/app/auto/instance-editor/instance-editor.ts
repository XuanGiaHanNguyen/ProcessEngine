import { Component, OnDestroy, computed, effect, inject, signal } from '@angular/core';

import { ApiError, ApiService } from '../../api/api.service';
import { Amounts, Instance, Process } from '../../api/models';
import { WorkspaceStore } from '../../state/workspace.store';

type AmountSide = 'inputs' | 'outputs';

/** Parses a number input; empty or invalid -> fallback. */
function num(value: string, fallback = 0): number {
  const n = Number(value);
  return value.trim() === '' || !Number.isFinite(n) ? fallback : Math.trunc(n);
}

function uniqueId(base: string, taken: string[]): string {
  let i = taken.length + 1;
  while (taken.includes(`${base}${i}`)) i++;
  return `${base}${i}`;
}

function renameKey(amounts: Amounts, from: string, to: string): Amounts {
  const out: Amounts = {};
  for (const [k, v] of Object.entries(amounts)) out[k === from ? to : k] = v;
  return out;
}

function withoutKey(amounts: Amounts, key: string): Amounts {
  const { [key]: _removed, ...rest } = amounts;
  return rest;
}

/**
 * Edits the workspace instance: settings, resources, processes (with inputs/outputs),
 * sample loading, random generation, and JSON import/export. Every change is validated by
 * the server (debounced), so the editor shows exactly the engine's error messages.
 */
@Component({
  selector: 'app-instance-editor',
  templateUrl: './instance-editor.html',
  styleUrl: './instance-editor.scss',
})
export class InstanceEditor implements OnDestroy {
  protected readonly store = inject(WorkspaceStore);
  private readonly api = inject(ApiService);

  protected readonly instance = this.store.instance;
  protected readonly errors = this.store.instanceErrors;
  protected readonly validating = this.store.instanceValidating;
  protected readonly message = signal<{ kind: 'error' | 'info'; text: string } | null>(null);
  protected readonly busy = signal(false);
  protected readonly showJson = signal(false);
  protected readonly jsonText = signal('');
  protected readonly genSize = signal<'small' | 'medium' | 'large'>('medium');
  protected readonly genSeed = signal(1);
  protected readonly selectedSample = signal('');

  protected readonly resourceIds = computed(() => this.instance().resources.map((r) => r.id));

  private validateTimer?: ReturnType<typeof setTimeout>;

  constructor() {
    // Debounced server-side validation of every edit.
    effect(() => {
      const snapshot = this.instance();
      clearTimeout(this.validateTimer);
      this.validating.set(true);
      this.validateTimer = setTimeout(() => void this.validate(snapshot), 250);
    });
  }

  ngOnDestroy(): void {
    clearTimeout(this.validateTimer);
  }

  private async validate(instance: Instance): Promise<void> {
    try {
      const result = await this.api.validateInstance(instance);
      if (instance === this.instance()) this.errors.set(result.errors);
    } catch (e) {
      this.errors.set([e instanceof ApiError ? e.reason : String(e)]);
    } finally {
      if (instance === this.instance()) this.validating.set(false);
    }
  }

  private edit(fn: (i: Instance) => Instance): void {
    this.store.updateInstance(fn);
  }

  // --- Loading -------------------------------------------------------------------------------

  protected async loadSample(id: string): Promise<void> {
    if (!id) return;
    await this.guard(async () => {
      await this.store.loadSample(id);
      this.message.set({ kind: 'info', text: `Loaded sample “${this.instance().name}”.` });
    });
  }

  protected async generate(): Promise<void> {
    await this.guard(async () => {
      const instance = await this.api.generate({ size: this.genSize(), seed: this.genSeed() });
      this.store.setInstance(instance);
      this.selectedSample.set('');
      this.message.set({
        kind: 'info',
        text: `Generated a ${this.genSize()} instance (seed ${this.genSeed()}).`,
      });
    });
  }

  protected newBlank(): void {
    this.store.setInstance({
      name: 'Untitled',
      resources: [],
      processes: [],
      start_inventory: {},
      workers: 1,
      horizon: 10,
      start_money: 0,
    });
    this.selectedSample.set('');
  }

  private async guard(fn: () => Promise<void>): Promise<void> {
    this.busy.set(true);
    this.message.set(null);
    try {
      await fn();
    } catch (e) {
      this.message.set({ kind: 'error', text: e instanceof ApiError ? e.reason : String(e) });
    } finally {
      this.busy.set(false);
    }
  }

  // --- JSON import / export ------------------------------------------------------------------

  protected openJson(): void {
    this.jsonText.set(JSON.stringify(this.instance(), null, 2));
    this.showJson.set(true);
  }

  protected applyJson(): void {
    try {
      const parsed = JSON.parse(this.jsonText()) as Instance;
      this.store.setInstance(normalize(parsed));
      this.showJson.set(false);
      this.message.set({ kind: 'info', text: 'Imported instance from JSON.' });
    } catch (e) {
      this.message.set({ kind: 'error', text: `Invalid JSON: ${(e as Error).message}` });
    }
  }

  protected async importFile(event: Event): Promise<void> {
    const file = (event.target as HTMLInputElement).files?.[0];
    if (!file) return;
    this.jsonText.set(await file.text());
    this.applyJson();
    (event.target as HTMLInputElement).value = '';
  }

  protected exportJson(): void {
    const blob = new Blob([JSON.stringify(this.instance(), null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${this.instance().name.replace(/[^\w-]+/g, '_') || 'instance'}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  // --- Settings ------------------------------------------------------------------------------

  protected setName(v: string): void {
    this.edit((i) => ({ ...i, name: v }));
  }
  protected setNumber(field: 'workers' | 'horizon' | 'start_money', v: string): void {
    this.edit((i) => ({ ...i, [field]: num(v) }));
  }

  // --- Resources -----------------------------------------------------------------------------

  protected addResource(): void {
    this.edit((i) => {
      const id = uniqueId(
        'r',
        i.resources.map((r) => r.id),
      );
      return { ...i, resources: [...i.resources, { id, name: id, sell_price: 1 }] };
    });
  }

  protected renameResource(index: number, to: string): void {
    this.edit((i) => {
      const from = i.resources[index].id;
      if (!to || to === from) return i;
      const inventory = renameKey(i.start_inventory, from, to);
      return {
        ...i,
        resources: i.resources.map((r, k) => (k === index ? { ...r, id: to } : r)),
        start_inventory: inventory,
        processes: i.processes.map((p) => ({
          ...p,
          inputs: renameKey(p.inputs, from, to),
          outputs: renameKey(p.outputs, from, to),
        })),
      };
    });
  }

  protected setResource(index: number, field: 'name' | 'sell_price', v: string): void {
    this.edit((i) => ({
      ...i,
      resources: i.resources.map((r, k) =>
        k === index ? { ...r, [field]: field === 'sell_price' ? num(v) : v } : r,
      ),
    }));
  }

  protected setStart(id: string, v: string): void {
    this.edit((i) => {
      const qty = num(v);
      const inventory =
        qty === 0 ? withoutKey(i.start_inventory, id) : { ...i.start_inventory, [id]: qty };
      return { ...i, start_inventory: inventory };
    });
  }

  protected removeResource(index: number): void {
    this.edit((i) => {
      const id = i.resources[index].id;
      return {
        ...i,
        resources: i.resources.filter((_, k) => k !== index),
        start_inventory: withoutKey(i.start_inventory, id),
        processes: i.processes.map((p) => ({
          ...p,
          inputs: withoutKey(p.inputs, id),
          outputs: withoutKey(p.outputs, id),
        })),
      };
    });
  }

  // --- Processes -----------------------------------------------------------------------------

  protected addProcess(): void {
    this.edit((i) => {
      const id = uniqueId(
        'p',
        i.processes.map((p) => p.id),
      );
      const first = i.resources[0]?.id;
      const second = i.resources[1]?.id ?? first;
      const process: Process = {
        id,
        name: id,
        inputs: first ? { [first]: 1 } : {},
        outputs: second ? { [second]: 1 } : {},
        duration: 1,
      };
      return { ...i, processes: [...i.processes, process] };
    });
  }

  private setProcess(index: number, fn: (p: Process) => Process): void {
    this.edit((i) => ({ ...i, processes: i.processes.map((p, k) => (k === index ? fn(p) : p)) }));
  }

  protected setProcessText(index: number, field: 'id' | 'name', v: string): void {
    this.setProcess(index, (p) => ({ ...p, [field]: v }));
  }
  protected setDuration(index: number, v: string): void {
    this.setProcess(index, (p) => ({ ...p, duration: num(v, 1) }));
  }
  protected setMaxRuns(index: number, v: string): void {
    this.setProcess(index, (p) => {
      const { max_runs: _drop, ...rest } = p;
      return v.trim() === '' ? rest : { ...rest, max_runs: num(v) };
    });
  }
  protected removeProcess(index: number): void {
    this.edit((i) => ({ ...i, processes: i.processes.filter((_, k) => k !== index) }));
  }

  protected entries(amounts: Amounts): [string, number][] {
    return Object.entries(amounts);
  }

  protected setAmountQty(index: number, side: AmountSide, resource: string, v: string): void {
    this.setProcess(index, (p) => ({ ...p, [side]: { ...p[side], [resource]: num(v, 1) } }));
  }
  protected setAmountResource(index: number, side: AmountSide, from: string, to: string): void {
    this.setProcess(index, (p) => {
      if (to === from || to in p[side]) return p;
      return { ...p, [side]: renameKey(p[side], from, to) };
    });
  }
  protected removeAmount(index: number, side: AmountSide, resource: string): void {
    this.setProcess(index, (p) => ({ ...p, [side]: withoutKey(p[side], resource) }));
  }
  protected addAmount(index: number, side: AmountSide): void {
    this.setProcess(index, (p) => {
      const free = this.resourceIds().find((id) => !(id in p[side]));
      return free ? { ...p, [side]: { ...p[side], [free]: 1 } } : p;
    });
  }
}

/** Fills optional fields so a hand-written JSON document fits the editor. */
export function normalize(raw: Partial<Instance>): Instance {
  return {
    name: raw.name ?? 'Imported',
    resources: (raw.resources ?? []).map((r) => ({
      id: r.id,
      name: r.name ?? r.id,
      sell_price: r.sell_price ?? 0,
    })),
    processes: (raw.processes ?? []).map((p) => ({
      ...p,
      name: p.name ?? p.id,
      inputs: p.inputs ?? {},
      outputs: p.outputs ?? {},
    })),
    start_inventory: raw.start_inventory ?? {},
    workers: raw.workers ?? 1,
    horizon: raw.horizon ?? 0,
    start_money: raw.start_money ?? 0,
    ...(raw.description ? { description: raw.description } : {}),
  };
}
