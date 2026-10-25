import { DestroyRef, Injectable, computed, inject, signal } from '@angular/core';

import { ApiService } from '../api/api.service';
import * as ops from './design-ops';
import { Design, DesignCheck, FactoryLevel, FactoryLevelInfo, SimResult } from './factory-models';
import { objectiveMet } from './objectives';

/** What is selected on the floor: a node (supplier, slot or market) or a belt. */
export type Selection = { kind: 'node'; id: string } | { kind: 'belt'; index: number } | null;

/** A tested design and its 24-hour report. */
export interface TestRun {
  id: number;
  name: string;
  kind: 'initial' | 'player' | 'solver';
  design: Design;
  result: SimResult;
}

const HISTORY_LIMIT = 100;
const MAX_TESTS = 20;
const PLAY_INTERVAL_MS = 700;

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
 * server-side check, the selection, and the tested factories with their playback position.
 * Drafts, tests and stepped hours are remembered per level in localStorage.
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

  readonly tests = signal<TestRun[]>([]);
  /** The test being played back, or null while editing the draft. */
  readonly viewing = signal<number | null>(null);
  readonly hour = signal(0);
  readonly playing = signal(false);
  readonly testing = signal(false);
  readonly comparing = signal(false);
  /** Upper bound on any design's profit, from the last comparison. */
  readonly bound = signal<number | null>(null);
  /** Distinct hours the player has stepped or played through (for "step" objectives). */
  private readonly stepped = signal<number[]>([]);
  private playTimer?: ReturnType<typeof setInterval>;

  private readonly past = signal<Design[]>([]);
  private readonly future = signal<Design[]>([]);
  private checkSeq = 0;

  readonly canUndo = computed(() => this.past().length > 0);
  readonly canRedo = computed(() => this.future().length > 0);
  readonly buildCost = computed(() => {
    const level = this.level();
    const draft = this.draft();
    return level && draft ? ops.buildCost(level, draft) : 0;
  });
  readonly initialDesign = computed(
    () => this.level()?.reference_designs.find((r) => r.initial) ?? null,
  );
  readonly viewedTest = computed(() => this.tests().find((t) => t.id === this.viewing()) ?? null);
  readonly currentHour = computed(() => this.viewedTest()?.result.hours[this.hour()] ?? null);
  readonly objectives = computed(() => {
    const player = this.tests()
      .filter((t) => t.kind === 'player')
      .map((t) => t.result);
    const stepped = this.stepped().length;
    return (this.level()?.objectives ?? []).map((o) => ({
      ...o,
      met: objectiveMet(o, player, stepped),
    }));
  });

  constructor() {
    inject(DestroyRef).onDestroy(() => clearInterval(this.playTimer));
  }

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
    this.pause();
    this.viewing.set(null);
    this.hour.set(0);
    this.loadTests(level);
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
    if (!this.tests().length && initial) await this.addTest('Initial', 'initial', initial, false);
  }

  /** Simulates the draft as a new test and starts playing it back from hour 1. */
  async runTest(): Promise<void> {
    const draft = this.draft();
    if (!draft || this.testing()) return;
    const n = this.tests().filter((t) => t.kind === 'player').length + 1;
    await this.addTest(`Test ${n}`, 'player', draft);
  }

  /** Simulates `design` and adds it to the tested factories (and plays it back if `show`). */
  async addTest(
    name: string,
    kind: TestRun['kind'],
    design: Design,
    show = true,
  ): Promise<TestRun | null> {
    const level = this.level();
    if (!level) return null;
    this.testing.set(true);
    try {
      const { result } = await this.api.simulateDesign(level.id, design);
      const ids = this.tests().map((t) => t.id);
      const run: TestRun = {
        id: ids.length ? Math.max(...ids) + 1 : 1,
        name,
        kind,
        design: ops.cloneDesign(design),
        result,
      };
      // Keep the initial test; drop the oldest others beyond the cap.
      this.tests.update((list) => {
        const next = [...list, run];
        while (next.length > MAX_TESTS)
          next.splice(
            next.findIndex((t) => t.kind !== 'initial'),
            1,
          );
        return next;
      });
      this.saveTests();
      if (show) this.view(run.id);
      return run;
    } catch (e) {
      this.message.set(e instanceof Error ? e.message : String(e));
      return null;
    } finally {
      this.testing.set(false);
    }
  }

  /**
   * Runs the factory solvers and adds each one's design as a test, so the player can play
   * them back like their own.
   */
  async compare(timeLimitMs = 3000): Promise<void> {
    const level = this.level();
    if (!level || this.comparing()) return;
    this.comparing.set(true);
    try {
      const res = await this.api.solveFactory(
        level.id,
        ['greedy', 'local_search', 'flow_lp'],
        timeLimitMs,
      );
      this.bound.set(res.bound);
      this.tests.update((list) => list.filter((t) => t.kind !== 'solver'));
      let best: TestRun | null = null;
      for (const r of res.results.filter((x) => x.valid)) {
        const run = await this.addTest(r.solver_name, 'solver', r.design, false);
        if (run && (!best || run.result.profit > best.result.profit)) best = run;
      }
      if (best) this.view(best.id);
    } catch (e) {
      this.message.set(e instanceof Error ? e.message : String(e));
    } finally {
      this.comparing.set(false);
    }
  }

  /** Plays back a test (the floor shows its design, read-only). */
  view(id: number): void {
    this.pause();
    this.viewing.set(id);
    this.selection.set(null);
    this.seek(0);
  }

  /** Leaves playback and returns to the draft. */
  backToDraft(): void {
    this.pause();
    this.viewing.set(null);
    this.selection.set(null);
  }

  /** Copies a test's design into the draft (undoable) and returns to editing. */
  editFrom(id: number): void {
    const t = this.tests().find((x) => x.id === id);
    if (!t) return;
    this.backToDraft();
    this.replaceDraft(t.design);
  }

  seek(hour: number): void {
    const hours = this.viewedTest()?.result.hours.length ?? 0;
    if (!hours) return;
    const h = Math.min(hours - 1, Math.max(0, hour));
    this.hour.set(h);
    if (!this.stepped().includes(h)) {
      this.stepped.update((s) => [...s, h]);
      this.saveTests();
    }
  }

  stepBy(delta: number): void {
    this.pause();
    this.seek(this.hour() + delta);
  }

  togglePlay(): void {
    if (this.playing()) {
      this.pause();
      return;
    }
    const last = (this.viewedTest()?.result.hours.length ?? 1) - 1;
    if (this.hour() >= last) this.seek(0);
    this.playing.set(true);
    this.playTimer = setInterval(() => {
      if (this.hour() >= last) this.pause();
      else this.seek(this.hour() + 1);
    }, PLAY_INTERVAL_MS);
  }

  pause(): void {
    clearInterval(this.playTimer);
    this.playing.set(false);
  }

  private loadTests(level: FactoryLevel): void {
    this.tests.set([]);
    this.stepped.set([]);
    this.bound.set(null);
    const saved = storageGet(`pf.factory.tests.${level.id}`);
    if (!saved) return;
    try {
      const data = JSON.parse(saved) as { tests: TestRun[]; stepped: number[] };
      this.tests.set(Array.isArray(data.tests) ? data.tests : []);
      this.stepped.set(Array.isArray(data.stepped) ? data.stepped : []);
    } catch {
      // A corrupt entry is ignored; the level starts with no tests.
    }
  }

  private saveTests(): void {
    const level = this.level();
    if (!level) return;
    storageSet(
      `pf.factory.tests.${level.id}`,
      JSON.stringify({ tests: this.tests(), stepped: this.stepped() }),
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
