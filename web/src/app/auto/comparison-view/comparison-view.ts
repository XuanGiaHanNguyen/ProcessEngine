import { Component, computed, input, output } from '@angular/core';
import { ChartConfiguration } from 'chart.js';

import { ProgressEvent } from '../../api/models';
import { ChartComponent } from '../../shared/chart';
import { colorFor } from '../../shared/palette';
import { ComparisonRow } from '../../state/solve-run';

/**
 * Side-by-side comparison of a job's solvers: table (earnings, gap, proof, runtime), bar
 * charts, and the live convergence chart (best-so-far vs. time) fed by the WebSocket.
 */
@Component({
  selector: 'app-comparison-view',
  imports: [ChartComponent],
  template: `
    <section class="card" aria-labelledby="compare-title">
      <h2 id="compare-title">Comparison</h2>
      @if (rows().length === 0) {
        <p class="empty">Run several solvers to compare them.</p>
      } @else {
        <div class="table-scroll">
          <table data-testid="comparison-table">
            <caption class="sr-only">
              Solver comparison
            </caption>
            <thead>
              <tr>
                <th scope="col">Solver</th>
                <th scope="col">Status</th>
                <th scope="col" class="num">Earnings</th>
                <th scope="col" class="num">Gap</th>
                <th scope="col">Proven</th>
                <th scope="col" class="num">Runtime</th>
              </tr>
            </thead>
            <tbody>
              @for (r of rows(); track r.solver) {
                <tr [class.selected]="r.solver === selected()">
                  <th scope="row">
                    <button
                      type="button"
                      class="link mono"
                      (click)="pick.emit(r.solver)"
                      [attr.aria-pressed]="r.solver === selected()"
                    >
                      {{ r.solver }}
                    </button>
                  </th>
                  <td>
                    @if (r.status === 'running' || r.status === 'queued') {
                      <span class="spinner" aria-hidden="true"></span>
                    }
                    {{ r.status }}
                  </td>
                  <td class="num" [attr.data-testid]="'earnings-' + r.solver">
                    @if (r.solution?.valid) {
                      {{ r.solution!.earnings }}
                    } @else if (r.solution) {
                      <span class="badge bad" [title]="r.solution.error ?? ''">failed</span>
                    } @else {
                      –
                    }
                  </td>
                  <td class="num">{{ r.gapPct === null ? '–' : r.gapPct.toFixed(2) + '%' }}</td>
                  <td>
                    @if (r.solution?.proven_optimal) {
                      <span class="badge ok">yes</span>
                    } @else if (r.solution) {
                      <span class="muted">no</span>
                    }
                  </td>
                  <td class="num">
                    {{ r.solution ? r.solution.runtime_ms.toFixed(1) + ' ms' : '–' }}
                  </td>
                </tr>
              }
            </tbody>
          </table>
        </div>
        <p class="small muted">Gap is measured against {{ referenceText() }}.</p>
        <div class="charts">
          @if (earningsChart(); as cfg) {
            <div>
              <h3>Earnings</h3>
              <app-chart [config]="cfg" [height]="200" [label]="earningsSummary()" />
            </div>
          }
          @if (runtimeChart(); as cfg) {
            <div>
              <h3>Runtime (ms, log scale)</h3>
              <app-chart [config]="cfg" [height]="200" label="Runtime per solver in milliseconds" />
            </div>
          }
        </div>
        @if (convergenceChart(); as cfg) {
          <h3>Convergence (best so far over time)</h3>
          <app-chart
            [config]="cfg"
            [height]="220"
            label="Best earnings found by each solver over elapsed time"
          />
        }
      }
    </section>
  `,
  styles: `
    .table-scroll {
      overflow-x: auto;
    }
    tr.selected {
      background: var(--accent-soft);
    }
    .link {
      border: 0;
      background: none;
      padding: 0;
      color: var(--accent-strong);
      text-decoration: underline;
      cursor: pointer;
    }
    .charts {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
      gap: 1rem;
    }
    h3 {
      margin-top: 0.6rem;
    }
  `,
})
export class ComparisonView {
  readonly rows = input.required<ComparisonRow[]>();
  readonly events = input<ProgressEvent[]>([]);
  readonly reference = input<number | null>(null);
  readonly referenceKind = input<'proven' | 'best'>('best');
  readonly selected = input<string | null>(null);
  readonly pick = output<string>();

  private readonly done = computed(() => this.rows().filter((r) => r.solution?.valid));

  protected readonly referenceText = computed(() => {
    const kind =
      this.referenceKind() === 'proven' ? 'the proven optimum' : 'the best schedule found';
    const ref = this.reference();
    return ref === null ? kind : `${kind} (${ref})`;
  });

  protected readonly earningsSummary = computed(
    () =>
      'Earnings per solver: ' +
      this.done()
        .map((r) => `${r.solver} ${r.solution!.earnings}`)
        .join(', '),
  );

  protected readonly earningsChart = computed<ChartConfiguration | null>(() => {
    const rows = this.done();
    if (!rows.length) return null;
    return {
      type: 'bar',
      data: {
        labels: rows.map((r) => r.solver),
        datasets: [
          {
            label: 'earnings',
            data: rows.map((r) => r.solution!.earnings),
            backgroundColor: rows.map((r) => colorFor(this.rows().indexOf(r))),
          },
        ],
      },
      options: { plugins: { legend: { display: false } }, scales: { y: { beginAtZero: true } } },
    };
  });

  protected readonly runtimeChart = computed<ChartConfiguration | null>(() => {
    const rows = this.done();
    if (!rows.length) return null;
    return {
      type: 'bar',
      data: {
        labels: rows.map((r) => r.solver),
        datasets: [
          {
            label: 'runtime (ms)',
            data: rows.map((r) => Math.max(0.001, r.solution!.runtime_ms)),
            backgroundColor: rows.map((r) => colorFor(this.rows().indexOf(r))),
          },
        ],
      },
      options: { plugins: { legend: { display: false } }, scales: { y: { type: 'logarithmic' } } },
    };
  });

  protected readonly convergenceChart = computed<ChartConfiguration | null>(() => {
    const bySolver = new Map<string, { x: number; y: number }[]>();
    for (const e of this.events()) {
      if (!e.solver || (e.event !== 'improved' && e.event !== 'solver_done')) continue;
      const list = bySolver.get(e.solver) ?? [];
      list.push({ x: e.elapsedMs, y: e.bestEarnings });
      bySolver.set(e.solver, list);
    }
    if (bySolver.size === 0) return null;
    const order = this.rows().map((r) => r.solver);
    return {
      type: 'line',
      data: {
        datasets: [...bySolver.entries()].map(([solver, points]) => ({
          label: solver,
          data: points,
          stepped: 'before' as const,
          borderColor: colorFor(order.indexOf(solver)),
          backgroundColor: colorFor(order.indexOf(solver)),
          pointRadius: 2,
        })),
      },
      options: {
        parsing: false,
        scales: {
          x: { type: 'linear', title: { display: true, text: 'elapsed ms' } },
          y: { title: { display: true, text: 'best earnings' } },
        },
        plugins: { legend: { position: 'bottom' } },
      },
    };
  });
}
