import { Injectable, computed, inject, signal } from '@angular/core';

import { ApiService } from '../api/api.service';
import * as ops from './design-ops';
import { Design, DesignCheck, FactoryLevel, FactoryLevelInfo } from './factory-models';

/** What is selected on the floor: a node (supplier, slot or market) or a belt. */
export type Selection = { kind: 'node'; id: string } | { kind: 'belt'; index: number } | null;

const HISTORY_LIMIT = 100;

function storageGet(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

function storageSet(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    // Private mode or storage disabled: drafts just are not remembered.
  }
}

/**
 * State of Factory mode for one page: the open level, the draft design with undo/redo, its
 * server-side check, and the selection. Drafts are remembered per level in localStorage.
 */
@Injectable()
export class FactoryStore {
  private readonly api = inject(ApiService);

  readonly levels = signal<FactoryLevelInfo[]>([]);
  readonly level = signal<FactoryLevel | null>(null);
  readonly draft = signal<Design | null>(null);
  readonly check = signal<DesignCheck | null>(null);
  readonly selection = signal<Selection>(null);
  readonly message = signal<string | null>(null);
  readonly error = signal<string | null>(null);

  private readonly past = signal<Design[]>([]);
  private readonly future = signal<Design[]>([]);
  private checkSeq = 0;

  readonly canUndo = computed(() => this.past().length > 0);
  readonly canRedo = computed(() => this.future().length > 0);
  readonly levelIndex = computed(() => this.levels().findIndex((l) => l.id === this.level()?.id));
  readonly buildCost = computed(() => {
    const level = this.level();
    const draft = this.draft();
    return level && draft ? ops.buildCost(level, draft) : 0;
  });
  readonly initialDesign = computed(
    () => this.level()?.reference_designs.find((r) => r.initial) ?? null,
  );

  async init(): Promise<void> {
    try {
      const levels = await this.api.factoryLevels();
      this.levels.set(levels);
      const last = storageGet('pf.factory.level');
      const start = levels.find((l) => l.id === last) ?? levels[0];
      if (start) await this.open(start.id);
    } catch (e) {
      this.error.set(e instanceof Error ? e.message : String(e));
    }
  }

  /** Loads a level and its remembered draft (or the initial design). */
  async open(id: string): Promise<void> {
    const level = await this.api.factoryLevel(id);
    this.level.set(level);
    storageSet('pf.factory.level', id);
    this.selection.set(null);
    this.message.set(null);
    this.past.set([]);
    this.future.set([]);
    let draft: Design | null = null;
    const saved = storageGet(`pf.factory.draft.${id}`);
    if (saved) {
      try {
        draft = JSON.parse(saved) as Design;
      } catch {
        draft = null;
      }
    }
    const initial = level.reference_designs.find((r) => r.initial)?.design;
    this.setDraft(
      draft ?? (initial ? ops.cloneDesign(initial) : { orders: {}, machines: [], belts: [] }),
    );
  }

  /** Applies an edit, recording it for undo. */
  edit(change: (level: FactoryLevel, design: Design) => Design): void {
    const level = this.level();
    const current = this.draft();
    if (!level || !current) return;
    const next = change(level, current);
    if (next === current) return;
    this.past.update((p) => [...p.slice(-HISTORY_LIMIT + 1), current]);
    this.future.set([]);
    this.setDraft(next);
  }

  undo(): void {
    const past = this.past();
    const current = this.draft();
    if (!past.length || !current) return;
    this.past.set(past.slice(0, -1));
    this.future.update((f) => [current, ...f]);
    this.setDraft(past[past.length - 1]);
  }

  redo(): void {
    const [next, ...rest] = this.future();
    const current = this.draft();
    if (!next || !current) return;
    this.future.set(rest);
    this.past.update((p) => [...p, current]);
    this.setDraft(next);
  }

  /** Replaces the draft with a copy of `design` (undoable). */
  replaceDraft(design: Design): void {
    this.edit(() => ops.cloneDesign(design));
  }

  /** Draws belts between two nodes; shows why not if it cannot. */
  connect(from: string, to: string): void {
    const level = this.level();
    const draft = this.draft();
    if (!level || !draft) return;
    const res = ops.connect(level, draft, from, to);
    if ('error' in res) {
      this.message.set(res.error);
      return;
    }
    this.edit(() => res.design);
    this.message.set(null);
    this.selection.set({ kind: 'belt', index: res.design.belts.length - 1 });
  }

  private setDraft(design: Design): void {
    this.draft.set(design);
    const level = this.level();
    if (level) storageSet(`pf.factory.draft.${level.id}`, JSON.stringify(design));
    const sel = this.selection();
    if (sel?.kind === 'belt' && sel.index >= design.belts.length) this.selection.set(null);
    void this.runCheck();
  }

  private async runCheck(): Promise<void> {
    const level = this.level();
    const draft = this.draft();
    if (!level || !draft) return;
    const seq = ++this.checkSeq;
    try {
      const check = await this.api.validateDesign(level.id, draft);
      if (seq === this.checkSeq) this.check.set(check);
    } catch (e) {
      if (seq === this.checkSeq) this.error.set(e instanceof Error ? e.message : String(e));
    }
  }
}
