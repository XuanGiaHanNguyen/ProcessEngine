import { Injectable, computed, inject, signal } from '@angular/core';

import { ApiError, ApiService } from '../api/api.service';
import { Instance, SampleInfo, SolverInfo } from '../api/models';
import { SolveRun } from './solve-run';

/** The empty-but-valid starting point for the editor. */
export function blankInstance(): Instance {
  return {
    name: 'Untitled',
    resources: [
      { id: 'wood', name: 'Wood', sell_price: 1 },
      { id: 'plank', name: 'Plank', sell_price: 4 },
    ],
    processes: [
      { id: 'saw', name: 'Saw', inputs: { wood: 2 }, outputs: { plank: 1 }, duration: 2 },
    ],
    start_inventory: { wood: 4 },
    workers: 1,
    horizon: 4,
    start_money: 0,
  };
}

/**
 * State shared by Auto and Play mode: the instance being worked on, the server's catalog
 * (samples and solvers), and the current Auto-mode solve run.
 */
@Injectable({ providedIn: 'root' })
export class WorkspaceStore {
  private readonly api = inject(ApiService);

  readonly instance = signal<Instance>(blankInstance());
  /** Sample id the current instance came from, if unchanged since loading (for share links). */
  readonly sampleId = signal<string | null>(null);
  readonly samples = signal<SampleInfo[]>([]);
  readonly solvers = signal<SolverInfo[]>([]);
  readonly catalogError = signal<string | null>(null);
  readonly catalogLoading = signal(false);
  /** Server validation of the current instance (kept up to date by the editor). */
  readonly instanceErrors = signal<string[]>([]);
  readonly instanceValidating = signal(false);

  /** The Auto-mode job (also offered to Play mode for "Replay solver schedule"). */
  readonly run = new SolveRun(this.api);
  /** Solver whose result the results view shows. */
  readonly selectedSolver = signal<string | null>(null);

  readonly selectedSolution = computed(() => {
    const name = this.selectedSolver();
    const results = this.run.job()?.results ?? [];
    const pick =
      results.find((r) => r.solver === name && r.solution?.valid) ??
      [...results]
        .filter((r) => r.solution?.valid)
        .sort((a, b) => (b.solution?.earnings ?? 0) - (a.solution?.earnings ?? 0))[0];
    return pick?.solution ?? null;
  });

  async loadCatalog(): Promise<void> {
    if (this.catalogLoading()) return;
    this.catalogLoading.set(true);
    this.catalogError.set(null);
    try {
      const [samples, solvers] = await Promise.all([this.api.samples(), this.api.solvers()]);
      this.samples.set(samples);
      this.solvers.set(solvers);
    } catch (e) {
      this.catalogError.set(e instanceof ApiError ? e.reason : String(e));
    } finally {
      this.catalogLoading.set(false);
    }
  }

  async loadSample(id: string): Promise<void> {
    const instance = await this.api.sample(id);
    this.setInstance(instance, id);
  }

  /** Replaces the instance; results computed for the previous one are discarded. */
  setInstance(instance: Instance, sampleId: string | null = null): void {
    this.instance.set(instance);
    this.sampleId.set(sampleId);
    this.run.reset();
    this.selectedSolver.set(null);
  }

  /** Edits the instance in place (keeps the run so results stay visible until re-run). */
  updateInstance(fn: (draft: Instance) => Instance): void {
    this.instance.update(fn);
    this.sampleId.set(null);
  }
}
