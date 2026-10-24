import { Component, computed, effect, inject, input, output, signal } from '@angular/core';
import { ChartConfiguration } from 'chart.js';

import { ApiError, ApiService } from '../../api/api.service';
import { Instance, Solution, Timeline } from '../../api/models';
import { ChartComponent } from '../../shared/chart';
import { GanttBar, GanttComponent } from '../../shared/gantt';
import { colorFor, processColors } from '../../shared/palette';

/** Value of the untouched start inventory: the "do nothing" baseline. */
export function baselineEarnings(inst: Instance): number {
  let total = inst.start_money;
  for (const r of inst.resources) total += (inst.start_inventory[r.id] ?? 0) * r.sell_price;
  return total;
}

/**
 * Shows one solution: earnings summary, a Gantt timeline of runs per worker slot, and
 * inventory over time. The layout (slots, inventory per tick) comes from the engine via
 * /api/schedules/timeline.
 */
@Component({
  selector: 'app-results-view',
  imports: [GanttComponent, ChartComponent],
  templateUrl: './results-view.html',
  styleUrl: './results-view.scss',
})
export class ResultsView {
  private readonly api = inject(ApiService);

  readonly instance = input.required<Instance>();
  readonly solution = input<Solution | null>(null);
  /** Solutions the user can switch between. */
  readonly choices = input<Solution[]>([]);
  readonly select = output<string>();
  readonly replay = output<Solution>();

  protected readonly timeline = signal<Timeline | null>(null);
  protected readonly loading = signal(false);
  protected readonly error = signal<string | null>(null);

  protected readonly baseline = computed(() => baselineEarnings(this.instance()));

  protected readonly bars = computed<GanttBar[]>(() => {
    const t = this.timeline();
    const inst = this.instance();
    if (!t) return [];
    const colors = processColors(inst.processes.map((p) => p.id));
    const names = new Map(inst.processes.map((p) => [p.id, p.name || p.id]));
    return t.runs.map((r) => ({
      label: names.get(r.process_id) ?? r.process_id,
      start: r.start,
      end: r.end,
      slot: r.slot,
      color: colors.get(r.process_id) ?? '#475569',
    }));
  });

  protected readonly inventoryChart = computed<ChartConfiguration | null>(() => {
    const t = this.timeline();
    const inst = this.instance();
    if (!t || t.points.length === 0) return null;
    // Show the resources that ever change, capped for readability.
    const changing = inst.resources
      .filter((r) => new Set(t.points.map((p) => p.inventory[r.id] ?? 0)).size > 1)
      .slice(0, 10);
    return {
      type: 'line',
      data: {
        datasets: changing.map((r, i) => ({
          label: r.name || r.id,
          data: t.points.map((p) => ({ x: p.tick, y: p.inventory[r.id] ?? 0 })),
          stepped: 'before' as const,
          borderColor: colorFor(i),
          backgroundColor: colorFor(i),
          pointRadius: 2,
        })),
      },
      options: {
        parsing: false,
        scales: {
          x: { type: 'linear', min: 0, max: inst.horizon, title: { display: true, text: 'tick' } },
          y: { beginAtZero: true, title: { display: true, text: 'quantity' } },
        },
        plugins: { legend: { position: 'bottom' } },
      },
    };
  });

  constructor() {
    effect(() => {
      const solution = this.solution();
      const instance = this.instance();
      this.timeline.set(null);
      this.error.set(null);
      if (!solution?.valid) return;
      this.loading.set(true);
      this.api
        .timeline(instance, solution.runs)
        .then((t) => {
          if (this.solution() === solution) this.timeline.set(t);
        })
        .catch((e: unknown) => this.error.set(e instanceof ApiError ? e.reason : String(e)))
        .finally(() => this.loading.set(false));
    });
  }

  protected statEntries(s: Solution): [string, number][] {
    return Object.entries(s.stats);
  }
}
