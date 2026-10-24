import { Signal, computed, signal } from '@angular/core';
import { Subscription } from 'rxjs';

import { ApiError, ApiService } from '../api/api.service';
import { Instance, Job, ProgressEvent, SolveOptions, Solution } from '../api/models';

/** One solver's row in a comparison: its solution plus derived metrics. */
export interface ComparisonRow {
  solver: string;
  status: string;
  solution: Solution | null;
  /** Gap to the reference (proven optimum if any solver proved one, else best found), in %. */
  gapPct: number | null;
}

/**
 * A solve job as seen by the UI: submits it, follows its WebSocket stream, refreshes the
 * results as solvers finish, and can cancel it. Everything is exposed as signals.
 */
export class SolveRun {
  private readonly jobSignal = signal<Job | null>(null);
  private readonly eventsSignal = signal<ProgressEvent[]>([]);
  private readonly errorSignal = signal<string | null>(null);
  private readonly busySignal = signal(false);
  private stream?: Subscription;
  private poll?: ReturnType<typeof setInterval>;

  readonly job: Signal<Job | null> = this.jobSignal.asReadonly();
  readonly events: Signal<ProgressEvent[]> = this.eventsSignal.asReadonly();
  readonly error: Signal<string | null> = this.errorSignal.asReadonly();
  /** True from submit until the job is done or cancelled. */
  readonly busy: Signal<boolean> = this.busySignal.asReadonly();

  /** Best earnings and the reference used for gaps. */
  readonly reference = computed(() => {
    const results = this.jobSignal()?.results ?? [];
    let best: number | null = null;
    let proven: number | null = null;
    for (const r of results) {
      const s = r.solution;
      if (!s?.valid) continue;
      best = best === null ? s.earnings : Math.max(best, s.earnings);
      if (s.proven_optimal) proven = s.earnings;
    }
    return { best, proven, value: proven ?? best };
  });

  readonly rows = computed<ComparisonRow[]>(() => {
    const ref = this.reference().value;
    return (this.jobSignal()?.results ?? []).map((r) => ({
      solver: r.solver,
      status: r.status,
      solution: r.solution,
      gapPct:
        r.solution?.valid && ref !== null && ref > 0
          ? (100 * (ref - r.solution.earnings)) / ref
          : r.solution?.valid
            ? 0
            : null,
    }));
  });

  constructor(private readonly api: ApiService) {}

  /** Submits a new job, or follows an existing one (e.g. from /compare) if `jobId` is given. */
  async start(instance: Instance, solvers: string[], options: SolveOptions): Promise<void> {
    this.reset();
    this.busySignal.set(true);
    try {
      const { jobId } = await this.api.solve(instance, solvers, options);
      this.follow(jobId);
    } catch (e) {
      this.fail(e);
    }
  }

  follow(jobId: string): void {
    this.stop();
    this.busySignal.set(true);
    void this.refresh(jobId);
    this.stream = this.api.stream(jobId).subscribe({
      next: (event) => {
        this.eventsSignal.update((list) => [...list, event]);
        if (event.event !== 'improved' && event.event !== 'heartbeat') void this.refresh(jobId);
      },
      error: () => this.startPolling(jobId),  // fall back to polling if WebSockets fail
      complete: () => void this.refresh(jobId),
    });
    this.startPolling(jobId);
  }

  async cancel(): Promise<void> {
    const job = this.jobSignal();
    if (!job) return;
    try {
      this.jobSignal.set(await this.api.cancel(job.jobId));
    } catch (e) {
      this.fail(e);
    }
  }

  /** Stops following the job (does not cancel it on the server). */
  stop(): void {
    this.stream?.unsubscribe();
    this.stream = undefined;
    if (this.poll) clearInterval(this.poll);
    this.poll = undefined;
  }

  reset(): void {
    this.stop();
    this.jobSignal.set(null);
    this.eventsSignal.set([]);
    this.errorSignal.set(null);
    this.busySignal.set(false);
  }

  private startPolling(jobId: string): void {
    if (this.poll) return;
    this.poll = setInterval(() => void this.refresh(jobId), 1000);
  }

  private async refresh(jobId: string): Promise<void> {
    try {
      const job = await this.api.job(jobId);
      this.jobSignal.set(job);
      if (job.status === 'done' || job.status === 'cancelled') {
        this.busySignal.set(false);
        this.stop();
      }
    } catch (e) {
      this.fail(e);
    }
  }

  private fail(e: unknown): void {
    this.errorSignal.set(e instanceof ApiError ? e.reason : String(e));
    this.busySignal.set(false);
    this.stop();
  }
}
