import { Component, computed, inject } from '@angular/core';
import { Router } from '@angular/router';

import { Solution } from '../api/models';
import { WorkspaceStore } from '../state/workspace.store';
import { ComparisonView } from './comparison-view/comparison-view';
import { InstanceEditor } from './instance-editor/instance-editor';
import { ProcessGraph } from './process-graph/process-graph';
import { ResultsView } from './results-view/results-view';
import { SolverPanel } from './solver-panel/solver-panel';

/** Auto mode: edit an instance, run solvers, inspect and compare their schedules. */
@Component({
  selector: 'app-auto-page',
  imports: [InstanceEditor, ProcessGraph, SolverPanel, ResultsView, ComparisonView],
  template: `
    <h1 class="sr-only">Auto mode</h1>
    <div class="layout">
      <div class="col">
        <app-instance-editor />
        <app-solver-panel [instanceValid]="valid()" />
      </div>
      <div class="col">
        <app-process-graph [instance]="store.instance()" [solution]="store.selectedSolution()" />
        <app-comparison-view
          [rows]="store.run.rows()"
          [events]="store.run.events()"
          [reference]="store.run.reference().value"
          [referenceKind]="store.run.reference().proven !== null ? 'proven' : 'best'"
          [selected]="store.selectedSolution()?.solver_name ?? null"
          (pick)="store.selectedSolver.set($event)"
        />
      </div>
    </div>
    <app-results-view
      [instance]="store.instance()"
      [solution]="store.selectedSolution()"
      [choices]="choices()"
      (selectSolver)="store.selectedSolver.set($event)"
      (replay)="replay($event)"
    />
  `,
  styles: `
    .layout {
      display: grid;
      grid-template-columns: minmax(0, 1.1fr) minmax(0, 1fr);
      gap: 1rem;
      margin-bottom: 1rem;
    }
    .col {
      display: flex;
      flex-direction: column;
      gap: 1rem;
      min-width: 0;
    }
    @media (max-width: 1000px) {
      .layout {
        grid-template-columns: minmax(0, 1fr);
      }
    }
  `,
})
export class AutoPage {
  protected readonly store = inject(WorkspaceStore);
  private readonly router = inject(Router);

  protected readonly valid = computed(
    () => !this.store.instanceValidating() && this.store.instanceErrors().length === 0,
  );

  protected readonly choices = computed<Solution[]>(() =>
    (this.store.run.job()?.results ?? []).flatMap((r) => (r.solution?.valid ? [r.solution] : [])),
  );

  protected replay(solution: Solution): void {
    void this.router.navigate(['/play'], { queryParams: { replay: solution.solver_name } });
  }
}
