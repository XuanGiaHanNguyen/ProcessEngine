import { DestroyRef, Injectable, computed, inject, signal } from '@angular/core';

import { ApiService } from '../api/api.service';
import * as ops from './design-ops';
import {
  Design,
  DesignCheck,
  DesignHint,
  FactoryLevel,
  FactoryLevelInfo,
  FactorySolverInfo,
  SimResult,
} from './factory-models';
import { objectiveMet } from './objectives';

/** What is selected on the floor: a node (supplier, machine or market) or a belt. */
export type Selection = { kind: 'node'; id: string } | { kind: 'belt'; index: number } | null;

/** A tested design and its 24-hour report. */
export interface TestRun {
  id: number;
  name: string;
  kind: 'initial' | 'player' | 'solver';
  design: Design;
  result: SimResult;
}

/** One optimizer's result from an Auto run, linked to the test that plays it back. */
export interface AutoRun {
  solver: string;
  profit: number;
  runtimeMs: number;
  testId: number | null;
  error: string | null;
}

const HISTORY_LIMIT = 100;
const MAX_TESTS = 20;
const PLAY_INTERVAL_MS = 700;

/** Encodes a design for a share link (URL-safe base64 of its JSON). */
export function encodeDesign(design: Design): string {
  const bytes = new TextEncoder().encode(JSON.stringify(design));
  let bin = '';
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

/** Decodes encodeDesign()'s output, or null if it is not a design. */
export function decodeDesign(text: string): Design | null {
  try {
    const bin = atob(text.replace(/-/g, '+').replace(/_/g, '/'));
    const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
    return asDesign(JSON.parse(new TextDecoder().decode(bytes)));
  } catch {
    return null;
  }
}

/** `value` if it has the shape of a design (or wraps one as {design}), else null. */
export function asDesign(value: unknown): Design | null {
  const v = (value as { design?: unknown })?.design ?? value;
  const d = v as Partial<Design> | null;
  if (!d || typeof d !== 'object') return null;
  if (typeof d.orders !== 'object' || !Array.isArray(d.machines) || !Array.isArray(d.belts))
    return null;
  return { orders: d.orders ?? {}, machines: d.machines, belts: d.belts };
}

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
  /** Upper bound on any design's profit, from the last Auto run. */
  readonly bound = signal<number | null>(null);
  /** The optimizers the server offers. */
  readonly solvers = signal<FactorySolverInfo[]>([]);
  /** Results of the last Auto run. */
  readonly autoRuns = signal<AutoRun[]>([]);
  /** The last hint for the draft (cleared when the draft changes). */
  readonly hint = signal<DesignHint | null>(null);
  readonly hinting = signal(false);
  /** Playback speed: hours per tick of PLAY_INTERVAL_MS / speed. */
  readonly speed = signal(1);
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

  /**
   * Loads the levels and opens the last one played, or the level and design of a share
   * link (`?level=…&design=…`).
   */
  async init(shared?: { level: string | null; design: string | null }): Promise<void> {
    try {
      const levels = await this.api.factoryLevels();
      this.levels.set(levels);
      void this.api
        .factorySolvers()
        .then((s) => this.solvers.set(s))
        .catch(() => this.solvers.set([]));
      const last = storageGet('pf.factory.level');
      const sharedLevel = levels.find((l) => l.id === shared?.level);
      const start = sharedLevel ?? levels.find((l) => l.id === last) ?? levels[0];
      if (!start) return;
      await this.open(start.id);
      const design = sharedLevel && shared?.design ? decodeDesign(shared.design) : null;
      if (design) {
        this.replaceDraft(design);
        this.message.set('Loaded a shared factory. Test it to see how it does.');
      } else if (sharedLevel && shared?.design) {
        this.message.set('That share link is broken, so the saved draft is shown instead.');
      }
    } catch (e) {
      this.error.set(e instanceof Error ? e.message : String(e));
    }
  }

  /** A link that opens this level with the draft loaded. */
  shareLink(origin: string): string {
    const level = this.level();
    const draft = this.draft();
    if (!level || !draft) return origin;
    return `${origin}/factory?level=${encodeURIComponent(level.id)}&design=${encodeDesign(draft)}`;
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
   * Auto: runs the chosen optimizers and adds each one's design as a test, so the player
   * can play them back like their own, then plays back the best.
   */
  async runAuto(
    solvers = ['greedy', 'local_search', 'flow_lp'],
    timeLimitMs = 3000,
    seed?: number,
  ): Promise<void> {
    const level = this.level();
    if (!level || this.comparing() || !solvers.length) return;
    this.comparing.set(true);
    try {
      const res = await this.api.solveFactory(level.id, solvers, timeLimitMs, seed);
      // Shown in whole dollars, rounded down so it stays an upper bound.
      this.bound.set(Math.floor(res.bound / 100) * 100);
      this.tests.update((list) => list.filter((t) => t.kind !== 'solver'));
      const runs: AutoRun[] = [];
      let best: TestRun | null = null;
      for (const r of res.results) {
        const run = r.valid ? await this.addTest(r.solver_name, 'solver', r.design, false) : null;
        runs.push({
          solver: r.solver_name,
          profit: r.profit,
          runtimeMs: r.runtime_ms,
          testId: run?.id ?? null,
          error: r.valid ? null : (r.error ?? 'invalid design'),
        });
        if (run && (!best || run.result.profit > best.result.profit)) best = run;
      }
      this.autoRuns.set(runs);
      if (best) this.view(best.id);
    } catch (e) {
      this.message.set(e instanceof Error ? e.message : String(e));
    } finally {
      this.comparing.set(false);
    }
  }

  /** Asks the server for the single change that most improves the draft. */
  async askHint(): Promise<void> {
    const level = this.level();
    const draft = this.draft();
    if (!level || !draft || this.hinting()) return;
    if (!this.check()?.valid) {
      this.message.set('Fix the problems in the draft first, then ask for a hint.');
      return;
    }
    this.hinting.set(true);
    try {
      const hint = await this.api.factoryHint(level.id, draft);
      if (this.draft() === draft) this.hint.set(hint);
    } catch (e) {
      this.message.set(e instanceof Error ? e.message : String(e));
    } finally {
      this.hinting.set(false);
    }
  }

  /** Applies the current hint to the draft (undoable). */
  applyHint(): void {
    const hint = this.hint();
    if (!hint?.found) return;
    this.backToDraft();
    this.replaceDraft(hint.design);
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
    }, PLAY_INTERVAL_MS / this.speed());
  }

  /** Changes the playback speed, keeping playback going if it is. */
  setSpeed(speed: number): void {
    this.speed.set(speed);
    if (this.playing()) {
      this.pause();
      this.togglePlay();
    }
  }

  pause(): void {
    clearInterval(this.playTimer);
    this.playing.set(false);
  }

  private loadTests(level: FactoryLevel): void {
    this.tests.set([]);
    this.stepped.set([]);
    this.bound.set(null);
    this.autoRuns.set([]);
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
    this.hint.set(null);
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
