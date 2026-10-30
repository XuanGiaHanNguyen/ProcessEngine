import {
  ChangeDetectionStrategy,
  Component,
  DestroyRef,
  computed,
  inject,
  signal,
} from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import { lucideBot, lucideDownload, lucidePencil, lucidePlay } from '@ng-icons/lucide';

import { ComparisonChart, ComparisonRow } from './comparison-chart';
import { money } from './factory-models';
import { FactoryStore } from './factory-store';

/**
 * Auto: pick optimizers, a time limit and a seed, run them on the level, and compare every
 * tested factory against the best known profit and the upper bound. Each optimizer's
 * factory becomes a test the player can watch or edit.
 */
@Component({
  selector: 'app-factory-auto',
  imports: [ComparisonChart, NgIcon],
  viewProviders: [provideIcons({ lucideBot, lucideDownload, lucidePencil, lucidePlay })],
  changeDetection: ChangeDetectionStrategy.OnPush,
  styleUrl: './factory-panels.scss',
  template: `
    <h3><ng-icon name="lucideBot" /> Auto-build</h3>
    <p class="note">
      Let the optimizers design this factory. Each one's factory is added to your tests, to play
      back or edit.
    </p>
    <fieldset class="solver-list">
      <legend>Optimizers</legend>
      @for (s of store.solvers(); track s.name) {
        <label>
          <input type="checkbox" [checked]="picked().has(s.name)" (change)="toggle(s.name)" />
          <span
            ><strong>{{ s.name }}</strong> <small>{{ s.description }}</small></span
          >
        </label>
      }
    </fieldset>
    <div class="options">
      <label>
        Time each
        <select (change)="timeMs.set(+$any($event.target).value)">
          <option [value]="1000" [selected]="timeMs() === 1000">1 s</option>
          <option [value]="3000" [selected]="timeMs() === 3000">3 s</option>
          <option [value]="10000" [selected]="timeMs() === 10000">10 s</option>
        </select>
      </label>
      <label>
        Seed
        <input
          type="number"
          min="0"
          [value]="seed()"
          (change)="seed.set(Math.max(0, Math.floor(+$any($event.target).value || 0)))"
        />
      </label>
    </div>
    <button
      type="button"
      class="go"
      (click)="run()"
      [disabled]="store.comparing() || !picked().size"
    >
      @if (store.comparing()) {
        Solving… {{ elapsed() }} s
      } @else {
        <ng-icon name="lucideBot" /> Run optimizers
      }
    </button>
    @if (store.comparing()) {
      <div
        class="progress"
        role="progressbar"
        [attr.aria-valuenow]="elapsed()"
        [attr.aria-valuemax]="timeMs() / 1000"
      >
        <span [style.width.%]="(100 * elapsed() * 1000) / timeMs()"></span>
      </div>
    }

    @if (rows().length) {
      <h4>Every tested factory</h4>
      <app-comparison-chart
        [rows]="rows()"
        [active]="store.viewing()"
        [target]="target()"
        [bound]="store.bound()"
        (pick)="store.view($event)"
      />
    }
    @if (store.autoRuns().length) {
      <table class="runs">
        <thead>
          <tr>
            <th scope="col">Optimizer</th>
            <th scope="col">Profit</th>
            <th scope="col">Time</th>
            <th scope="col"><span class="visually-hidden">Actions</span></th>
          </tr>
        </thead>
        <tbody>
          @for (r of store.autoRuns(); track r.solver) {
            <tr>
              <td>{{ r.solver }}</td>
              @if (r.error) {
                <td colspan="2" class="bad">{{ r.error }}</td>
              } @else {
                <td>{{ money(r.profit) }}</td>
                <td>{{ (r.runtimeMs / 1000).toFixed(1) }} s</td>
              }
              <td class="acts">
                @if (r.testId !== null) {
                  <button
                    type="button"
                    (click)="store.view(r.testId)"
                    [attr.aria-label]="'Watch ' + r.solver"
                  >
                    <ng-icon name="lucidePlay" />
                  </button>
                  <button
                    type="button"
                    (click)="store.editFrom(r.testId)"
                    [attr.aria-label]="'Edit from ' + r.solver"
                  >
                    <ng-icon name="lucidePencil" />
                  </button>
                }
              </td>
            </tr>
          }
        </tbody>
      </table>
      @if (store.bound(); as bound) {
        <p class="note">No factory can earn more than {{ money(bound) }} here.</p>
      }
      <button type="button" class="soft" (click)="exportRuns()">
        <ng-icon name="lucideDownload" /> Export results (JSON)
      </button>
    }
  `,
})
export class FactoryAuto {
  protected readonly store = inject(FactoryStore);
  protected readonly money = money;
  protected readonly Math = Math;

  protected readonly picked = signal(new Set(['greedy', 'local_search', 'flow_lp']));
  protected readonly timeMs = signal(3000);
  protected readonly seed = signal(1);
  protected readonly elapsed = signal(0);
  private timer?: ReturnType<typeof setInterval>;

  protected readonly target = computed(
    () =>
      this.store.level()?.reference_designs.find((r) => r.name === 'Best known')?.profit ?? null,
  );
  protected readonly rows = computed<ComparisonRow[]>(() =>
    this.store.tests().map((t) => ({
      id: t.id,
      label: t.name,
      profit: t.result.profit,
      kind: t.kind,
    })),
  );

  constructor() {
    inject(DestroyRef).onDestroy(() => clearInterval(this.timer));
  }

  protected toggle(name: string): void {
    this.picked.update((s) => {
      const next = new Set(s);
      if (!next.delete(name)) next.add(name);
      return next;
    });
  }

  protected async run(): Promise<void> {
    this.elapsed.set(0);
    const start = Date.now();
    this.timer = setInterval(() => this.elapsed.set(Math.round((Date.now() - start) / 1000)), 250);
    try {
      // In the server's order, so results line up with the list above.
      const names = this.store
        .solvers()
        .map((s) => s.name)
        .filter((n) => this.picked().has(n));
      await this.store.runAuto(names, this.timeMs(), this.seed());
    } finally {
      clearInterval(this.timer);
    }
  }

  protected exportRuns(): void {
    const level = this.store.level();
    const data = {
      level: level?.id,
      bound: this.store.bound(),
      results: this.store.autoRuns().map((r) => ({
        ...r,
        design: this.store.tests().find((t) => t.id === r.testId)?.design ?? null,
      })),
    };
    download(`${level?.id ?? 'factory'}-auto.json`, data);
  }
}

/** Saves `data` as a JSON file through the browser. */
export function download(name: string, data: unknown): void {
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }),
  );
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}
