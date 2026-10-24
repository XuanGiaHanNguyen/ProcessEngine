import { Component, computed, inject, input, signal } from '@angular/core';

import { WorkspaceStore } from '../../state/workspace.store';

export const DEFAULT_SOLVERS = ['greedy', 'beam_search', 'branch_and_bound'];

/** Pick solvers, set time limit and seed, run and cancel. */
@Component({
  selector: 'app-solver-panel',
  template: `
    <section class="card" aria-labelledby="solvers-title">
      <h2 id="solvers-title">Solvers</h2>
      @if (store.catalogLoading()) {
        <p class="muted"><span class="spinner" aria-hidden="true"></span> Loading solvers…</p>
      } @else if (store.solvers().length === 0) {
        <p class="empty">
          No solvers available{{ store.catalogError() ? ': ' + store.catalogError() : '' }}.
        </p>
      } @else {
        <fieldset>
          <legend class="sr-only">Solvers to run</legend>
          <ul class="solvers">
            @for (s of store.solvers(); track s.name) {
              <li>
                <label [title]="s.description">
                  <input
                    type="checkbox"
                    [attr.data-testid]="'solver-' + s.name"
                    [checked]="selected().has(s.name)"
                    (change)="toggle(s.name, $any($event.target).checked)"
                  />
                  <span class="mono">{{ s.name }}</span>
                  @if (s.exact) {
                    <span class="badge ok">exact</span>
                  }
                </label>
                <span class="small muted desc">{{ s.description }}</span>
              </li>
            }
          </ul>
        </fieldset>
      }
      <div class="row options">
        <label class="field">
          Time limit (ms)
          <input
            type="number"
            min="10"
            step="100"
            data-testid="time-limit"
            [value]="timeLimit()"
            (input)="timeLimit.set(+$any($event.target).value)"
          />
        </label>
        <label class="field">
          Seed
          <input
            type="number"
            min="0"
            [value]="seed()"
            (input)="seed.set(+$any($event.target).value)"
          />
        </label>
        <span class="spacer"></span>
        @if (store.run.busy()) {
          <button type="button" class="danger" data-testid="cancel" (click)="store.run.cancel()">
            Cancel
          </button>
        }
        <button
          type="button"
          class="primary"
          data-testid="run"
          [disabled]="!canRun()"
          (click)="run()"
        >
          @if (store.run.busy()) {
            <span class="spinner" aria-hidden="true"></span> Running…
          } @else {
            Run {{ selected().size }} solver{{ selected().size === 1 ? '' : 's' }}
          }
        </button>
      </div>
      @if (!instanceValid()) {
        <p class="small muted">Fix the instance problems before running.</p>
      }
      @if (store.run.error(); as err) {
        <p class="alert" role="alert">{{ err }}</p>
      }
      @if (store.run.job(); as job) {
        <p class="small muted" role="status" aria-live="polite">
          Job {{ job.status }}:
          @for (r of job.results; track r.solver; let last = $last) {
            {{ r.solver }} {{ r.status }}{{ last ? '' : ', ' }}
          }
        </p>
      }
    </section>
  `,
  styles: `
    fieldset {
      border: 0;
      padding: 0;
      margin: 0;
    }
    .solvers {
      list-style: none;
      padding: 0;
      margin: 0 0 0.5rem;
      display: grid;
      gap: 0.25rem;
    }
    .solvers li {
      display: flex;
      flex-direction: column;
    }
    .solvers label {
      display: flex;
      gap: 0.4rem;
      align-items: center;
      cursor: pointer;
    }
    .desc {
      margin-left: 1.6rem;
    }
    .options {
      align-items: flex-end;
    }
  `,
})
export class SolverPanel {
  protected readonly store = inject(WorkspaceStore);
  /** Whether the instance passed server validation (set by the page). */
  readonly instanceValid = input(true);

  protected readonly selected = signal(new Set(DEFAULT_SOLVERS));
  protected readonly timeLimit = signal(3000);
  protected readonly seed = signal(1);

  protected readonly canRun = computed(
    () => this.selected().size > 0 && !this.store.run.busy() && this.instanceValid(),
  );

  protected toggle(name: string, on: boolean): void {
    this.selected.update((s) => {
      const next = new Set(s);
      if (on) next.add(name);
      else next.delete(name);
      return next;
    });
  }

  protected run(): void {
    this.store.selectedSolver.set(null);
    // Keep the registry order so tables and charts are stable.
    const names = this.store
      .solvers()
      .map((s) => s.name)
      .filter((n) => this.selected().has(n));
    void this.store.run.start(this.store.instance(), names, {
      timeLimitMs: this.timeLimit(),
      seed: this.seed(),
    });
  }
}
