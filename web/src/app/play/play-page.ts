import {
  Component,
  OnDestroy,
  OnInit,
  computed,
  effect,
  inject,
  input,
  signal,
} from '@angular/core';
import { Router } from '@angular/router';

import { ApiError, ApiService } from '../api/api.service';
import {
  Action,
  FinishResult,
  Hint,
  Instance,
  ProcessStatus,
  SessionView,
  Solution,
  Timeline,
} from '../api/models';
import { ComparisonView } from '../auto/comparison-view/comparison-view';
import { baselineEarnings } from '../auto/results-view/results-view';
import { DEFAULT_SOLVERS } from '../auto/solver-panel/solver-panel';
import { GanttBar, GanttComponent } from '../shared/gantt';
import { processColors } from '../shared/palette';
import { SolveRun } from '../state/solve-run';
import { WorkspaceStore } from '../state/workspace.store';
import {
  decodeActions,
  encodeActions,
  makeRunFile,
  parseRunFile,
  scheduleFromHistory,
} from './run-file';

type Mode = 'challenge' | 'assist';

interface LeaderboardRow {
  name: string;
  earnings: number;
  pct: number;
  proven: boolean;
  isPlayer: boolean;
  solution: Solution | null;
}

/**
 * Play mode: the player schedules runs by hand, tick by tick, on a server-side GameSession
 * (same rules as the validator). Challenge mode hides solver results until "Reveal";
 * Assist mode offers hints. Any solver schedule can be replayed step by step.
 */
@Component({
  selector: 'app-play-page',
  imports: [GanttComponent, ComparisonView],
  templateUrl: './play-page.html',
  styleUrl: './play-page.scss',
  host: { '(document:keydown)': 'onKey($event)' },
})
export class PlayPage implements OnInit, OnDestroy {
  private readonly api = inject(ApiService);
  private readonly router = inject(Router);
  protected readonly store = inject(WorkspaceStore);

  /** Query parameters (bound by the router). */
  readonly sample = input<string>();
  readonly actions = input<string>();
  readonly replay = input<string>();

  protected readonly view = signal<SessionView | null>(null);
  protected readonly instance = signal<Instance | null>(null);
  protected readonly sourceSample = signal<string | null>(null);
  protected readonly busy = signal(false);
  protected readonly error = signal<string | null>(null);
  protected readonly notice = signal<string | null>(null);
  protected readonly mode = signal<Mode>('challenge');
  protected readonly hint = signal<Hint | null>(null);
  protected readonly finished = signal<FinishResult | null>(null);
  /** The player's own final result, kept even if a solver schedule is replayed afterwards. */
  protected readonly playerResult = signal<Solution | null>(null);
  protected readonly replaying = signal<string | null>(null);
  protected readonly autoplay = signal(false);
  protected readonly timeline = signal<Timeline | null>(null);
  protected readonly reveal = new SolveRun(this.api);
  protected readonly revealSolvers = signal(new Set(DEFAULT_SOLVERS));

  private autoplayTimer?: ReturnType<typeof setInterval>;
  private timelineRequest = 0;

  protected readonly state = computed(() => this.view()?.state ?? null);
  protected readonly statusById = computed(
    () => new Map((this.view()?.processStatus ?? []).map((s) => [s.processId, s])),
  );
  protected readonly colors = computed(() =>
    processColors(this.instance()?.processes.map((p) => p.id) ?? []),
  );
  protected readonly nextEvent = computed(() => {
    const adv = this.view()?.legalActions.find((a) => a.type === 'advance_to');
    return adv?.type === 'advance_to' ? adv.tick : null;
  });
  protected readonly canWait = computed(
    () => this.view()?.legalActions.some((a) => a.type === 'wait') ?? false,
  );
  protected readonly historyLength = computed(() => this.state()?.history.length ?? 0);
  protected readonly timelineLength = computed(
    () => this.historyLength() + (this.state()?.redo_available ?? 0),
  );
  protected readonly baseline = computed(() => {
    const inst = this.instance();
    return inst ? baselineEarnings(inst) : 0;
  });
  protected readonly bestKnown = computed(() => {
    const ref = this.reveal.reference().value;
    if (ref !== null) return ref;
    const auto = this.store.run.reference().value;
    return this.instance() === null || this.store.instance() !== this.instance() ? null : auto;
  });
  protected readonly showSolverInfo = computed(
    () => this.mode() === 'assist' || this.reveal.job() !== null,
  );

  protected readonly workerSlots = computed(() => {
    const s = this.state();
    if (!s) return [];
    const inst = this.instance()!;
    const names = new Map(inst.processes.map((p) => [p.id, p]));
    const slots: ({
      process: string;
      name: string;
      start: number;
      end: number;
      remaining: number;
      pct: number;
    } | null)[] = [];
    for (const r of s.runs_in_progress) {
      const p = names.get(r.process_id);
      const duration = r.end - r.start;
      slots.push({
        process: r.process_id,
        name: p?.name ?? r.process_id,
        start: r.start,
        end: r.end,
        remaining: r.remaining,
        pct: Math.round((100 * (duration - r.remaining)) / Math.max(1, duration)),
      });
    }
    while (slots.length < s.workers) slots.push(null);
    return slots;
  });

  protected readonly bars = computed<GanttBar[]>(() => {
    const t = this.timeline();
    const inst = this.instance();
    if (!t || !inst) return [];
    const names = new Map(inst.processes.map((p) => [p.id, p.name || p.id]));
    return t.runs.map((r) => ({
      label: names.get(r.process_id) ?? r.process_id,
      start: r.start,
      end: r.end,
      slot: r.slot,
      color: this.colors().get(r.process_id) ?? '#475569',
    }));
  });

  protected readonly leaderboard = computed<LeaderboardRow[]>(() => {
    const player = this.playerResult();
    const rows: LeaderboardRow[] = [];
    for (const r of this.reveal.job()?.results ?? []) {
      if (r.solution?.valid) {
        rows.push({
          name: r.solver,
          earnings: r.solution.earnings,
          pct: 0,
          proven: r.solution.proven_optimal,
          isPlayer: false,
          solution: r.solution,
        });
      }
    }
    if (player)
      rows.push({
        name: 'You',
        earnings: player.earnings,
        pct: 0,
        proven: false,
        isPlayer: true,
        solution: player,
      });
    const best = Math.max(...rows.map((r) => r.earnings), 0);
    for (const r of rows) r.pct = best > 0 ? (100 * r.earnings) / best : 100;
    return rows.sort((a, b) => b.earnings - a.earnings || Number(b.isPlayer) - Number(a.isPlayer));
  });

  protected readonly playerPct = computed(
    () => this.leaderboard().find((r) => r.isPlayer)?.pct ?? null,
  );

  constructor() {
    // Keep the Gantt chart in sync with the session history.
    effect(() => {
      const s = this.state();
      const inst = this.instance();
      if (!s || !inst) return;
      const schedule = scheduleFromHistory(s.history, inst.horizon);
      const request = ++this.timelineRequest;
      this.api
        .timeline(inst, schedule)
        .then((t) => {
          if (request === this.timelineRequest) this.timeline.set(t);
        })
        .catch(() => {
          if (request === this.timelineRequest) this.timeline.set(null);
        });
    });
  }

  async ngOnInit(): Promise<void> {
    if (this.store.samples().length === 0) void this.store.loadCatalog();
    const sample = this.sample();
    if (sample) {
      await this.startFromSample(
        sample,
        decodeSafe(this.actions() ?? '', (m) => this.error.set(m)),
      );
    } else if (this.replay()) {
      await this.startFromWorkspace();
      const solution = this.store.run
        .job()
        ?.results.find((r) => r.solver === this.replay())?.solution;
      if (solution?.valid) await this.replaySolution(solution);
      else this.error.set(`No ${this.replay()} result to replay; run it in Auto mode first.`);
    }
  }

  ngOnDestroy(): void {
    this.stopAutoplay();
    this.reveal.stop();
  }

  // --- Starting a game ---------------------------------------------------------------------

  protected async startFromSample(id: string, actions: Action[] = []): Promise<void> {
    await this.task(async () => {
      const view = await this.api.createSession({ instanceId: id });
      this.begin(view, id);
      for (const a of actions) this.view.set(await this.api.act(view.sessionId, a));
    });
  }

  protected async startFromWorkspace(): Promise<void> {
    await this.task(async () => {
      const view = await this.api.createSession({ instance: this.store.instance() });
      this.begin(view, this.store.sampleId());
    });
  }

  protected async importRun(event: Event): Promise<void> {
    const file = (event.target as HTMLInputElement).files?.[0];
    (event.target as HTMLInputElement).value = '';
    if (!file) return;
    await this.task(async () => {
      const run = parseRunFile(await file.text());
      const view = await this.api.createSession({ instance: run.instance });
      this.begin(view, null);
      for (const a of run.actions) this.view.set(await this.api.act(view.sessionId, a));
      this.notice.set(`Imported a run with ${run.actions.length} actions.`);
    });
  }

  private begin(view: SessionView, sample: string | null): void {
    this.view.set(view);
    this.instance.set(view.instance ?? null);
    this.sourceSample.set(sample);
    this.finished.set(null);
    this.playerResult.set(null);
    this.hint.set(null);
    this.replaying.set(null);
    this.reveal.reset();
  }

  protected leave(): void {
    const id = this.view()?.sessionId;
    if (id) void this.api.deleteSession(id).catch(() => undefined);
    this.stopAutoplay();
    this.view.set(null);
    this.instance.set(null);
    this.timeline.set(null);
    void this.router.navigate(['/play']);
  }

  // --- Moves -------------------------------------------------------------------------------

  protected start(processId: string): Promise<void> {
    return this.act({ type: 'start_run', process_id: processId });
  }
  protected wait(): Promise<void> {
    return this.act({ type: 'wait' });
  }
  protected skip(): Promise<void> {
    const t = this.nextEvent();
    return t === null ? Promise.resolve() : this.act({ type: 'advance_to', tick: t });
  }

  private async act(action: Action): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id || this.busy()) return;
    await this.task(async () => {
      this.view.set(await this.api.act(id, action));
      this.hint.set(null);
    });
  }

  protected async undo(): Promise<void> {
    await this.session((id) => this.api.undo(id));
  }
  protected async redo(): Promise<void> {
    await this.session((id) => this.api.redo(id));
  }
  protected async restart(): Promise<void> {
    this.stopAutoplay();
    this.replaying.set(null);
    await this.session((id) => this.api.restart(id));
  }

  /** Scrubber: move through history with undo/redo. */
  protected async scrubTo(position: number): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id) return;
    await this.task(async () => {
      while (this.historyLength() > position) this.view.set(await this.api.undo(id));
      while (this.historyLength() < position && (this.state()?.redo_available ?? 0) > 0) {
        this.view.set(await this.api.redo(id));
      }
    });
  }

  private async session(op: (id: string) => Promise<SessionView>): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id || this.busy()) return;
    await this.task(async () => {
      this.view.set(await op(id));
      this.hint.set(null);
      if (!this.view()?.state.finished) this.finished.set(null);
    });
  }

  protected async askHint(): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id) return;
    await this.task(async () => this.hint.set(await this.api.hint(id)));
  }

  protected describe(action: Action): string {
    switch (action.type) {
      case 'start_run':
        return `Start “${this.instance()?.processes.find((p) => p.id === action.process_id)?.name ?? action.process_id}” now`;
      case 'wait':
        return 'Wait one tick';
      case 'advance_to':
        return `Skip to tick ${action.tick}`;
      case 'finish':
        return 'Finish: nothing more is worth starting';
    }
  }

  protected async applyHint(): Promise<void> {
    const h = this.hint();
    if (!h) return;
    if (h.suggestedAction.type === 'finish') await this.finish();
    else await this.act(h.suggestedAction);
  }

  protected async finish(): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id) return;
    this.stopAutoplay();
    await this.task(async () => {
      const result = await this.api.finish(id);
      this.view.set(result);
      this.finished.set(result);
      if (!this.replaying()) this.playerResult.set(result.solution);
    });
  }

  // --- Challenge: reveal solvers -------------------------------------------------------------

  protected toggleRevealSolver(name: string, on: boolean): void {
    this.revealSolvers.update((s) => {
      const next = new Set(s);
      if (on) next.add(name);
      else next.delete(name);
      return next;
    });
  }

  protected async revealNow(): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id) return;
    const names = this.store
      .solvers()
      .map((s) => s.name)
      .filter((n) => this.revealSolvers().has(n));
    await this.task(async () => {
      const r = await this.api.compare(id, names, { timeLimitMs: 3000 });
      this.reveal.follow(r.jobId);
    });
  }

  // --- Replay ------------------------------------------------------------------------------

  protected async replaySolution(solution: Solution): Promise<void> {
    const id = this.view()?.sessionId;
    if (!id) return;
    this.stopAutoplay();
    await this.task(async () => {
      this.view.set(await this.api.load(id, solution.runs));
      this.finished.set(null);
      this.replaying.set(solution.solver_name);
      this.notice.set(
        `Loaded ${solution.solver_name}'s schedule. Step through it with Redo or Play.`,
      );
    });
  }

  protected toggleAutoplay(): void {
    if (this.autoplay()) {
      this.stopAutoplay();
      return;
    }
    this.autoplay.set(true);
    this.autoplayTimer = setInterval(() => {
      if ((this.state()?.redo_available ?? 0) === 0) {
        this.stopAutoplay();
        return;
      }
      if (!this.busy()) void this.redo();
    }, 600);
  }

  private stopAutoplay(): void {
    this.autoplay.set(false);
    if (this.autoplayTimer) clearInterval(this.autoplayTimer);
    this.autoplayTimer = undefined;
  }

  // --- Sharing -----------------------------------------------------------------------------

  protected exportRun(): void {
    const inst = this.instance();
    const s = this.state();
    if (!inst || !s) return;
    const blob = new Blob([JSON.stringify(makeRunFile(inst, s.history), null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${inst.name.replace(/[^\w-]+/g, '_')}-run.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  protected readonly shareLink = computed(() => {
    const sample = this.sourceSample();
    const s = this.state();
    if (!sample || !s) return null;
    const params = new URLSearchParams({ sample, actions: encodeActions(s.history) });
    return `${location.origin}/play?${params.toString()}`;
  });

  protected async copyShareLink(): Promise<void> {
    const link = this.shareLink();
    if (!link) return;
    try {
      await navigator.clipboard.writeText(link);
      this.notice.set('Share link copied to the clipboard.');
    } catch {
      this.notice.set(`Share link: ${link}`);
    }
  }

  // --- Helpers -----------------------------------------------------------------------------

  protected statusOf(id: string): ProcessStatus | undefined {
    return this.statusById().get(id);
  }

  protected amounts(a: Record<string, number>): string {
    const inst = this.instance();
    const name = (id: string) => inst?.resources.find((r) => r.id === id)?.name ?? id;
    const parts = Object.entries(a).map(([id, q]) => `${q} ${name(id)}`);
    return parts.length ? parts.join(' + ') : 'nothing';
  }

  protected runsLeft(id: string): string | null {
    const p = this.instance()?.processes.find((x) => x.id === id);
    if (p?.max_runs === undefined || p.max_runs === null) return null;
    return `${p.max_runs - (this.state()?.run_counts[id] ?? 0)} of ${p.max_runs} runs left`;
  }

  protected inventoryRows = computed(() => {
    const s = this.state();
    const inst = this.instance();
    if (!s || !inst) return [];
    return inst.resources.map((r) => {
      const qty = s.inventory[r.id] ?? 0;
      return { id: r.id, name: r.name, qty, price: r.sell_price, value: qty * r.sell_price };
    });
  });

  private async task(fn: () => Promise<void>): Promise<void> {
    this.busy.set(true);
    this.error.set(null);
    try {
      await fn();
    } catch (e) {
      if (e instanceof ApiError) {
        this.error.set(e.reason);
        // An illegal action (409) carries the unchanged session view: resynchronize.
        const body = e.body as Partial<SessionView> | undefined;
        if (body?.state && body.sessionId === this.view()?.sessionId) {
          this.view.update((v) =>
            v ? ({ ...v, ...body, instance: v.instance } as SessionView) : v,
          );
        }
      } else {
        this.error.set(e instanceof Error ? e.message : String(e));
      }
    } finally {
      this.busy.set(false);
    }
  }

  /** Keyboard shortcuts (ignored while typing in a form field). */
  protected onKey(event: KeyboardEvent): void {
    const target = event.target as HTMLElement | null;
    if (!this.view() || event.metaKey || event.ctrlKey || event.altKey) return;
    if (target && ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName)) return;
    const key = event.key.toLowerCase();
    if (key === ' ' && this.canWait()) {
      event.preventDefault();
      void this.wait();
    } else if (key === 'n') void this.skip();
    else if (key === 'z' && !event.shiftKey) void this.undo();
    else if (key === 'y' || (key === 'z' && event.shiftKey)) void this.redo();
    else if (key === 'h' && this.mode() === 'assist') void this.askHint();
  }
}

function decodeSafe(text: string, onError: (m: string) => void): Action[] {
  try {
    return decodeActions(text);
  } catch (e) {
    onError((e as Error).message);
    return [];
  }
}
