import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { Component, input } from '@angular/core';
import { TestBed } from '@angular/core/testing';

import { Instance, Solution, Timeline } from '../../api/models';
import { ChartComponent } from '../../shared/chart';
import { ResultsView, baselineEarnings } from './results-view';

// jsdom has no canvas: replace the Chart.js wrapper with a stub.
@Component({ selector: 'app-chart', template: '<div class="chart-stub">{{ label() }}</div>' })
class ChartStub {
  readonly config = input<unknown>();
  readonly label = input('');
  readonly height = input(0);
}

const instance: Instance = {
  name: 'Sawmill',
  resources: [
    { id: 'wood', name: 'Wood', sell_price: 1 },
    { id: 'plank', name: 'Plank', sell_price: 4 },
  ],
  processes: [{ id: 'saw', name: 'Saw', inputs: { wood: 2 }, outputs: { plank: 1 }, duration: 2 }],
  start_inventory: { wood: 4 },
  workers: 1,
  horizon: 4,
  start_money: 0,
};

const solution: Solution = {
  runs: [
    { process_id: 'saw', start_tick: 0 },
    { process_id: 'saw', start_tick: 2 },
  ],
  earnings: 8,
  proven_optimal: true,
  runtime_ms: 0.4,
  solver_name: 'branch_and_bound',
  stats: { nodes: 5 },
  valid: true,
  error: null,
};

const timeline: Timeline = {
  earnings: 8,
  runs: [
    { run_index: 0, process_id: 'saw', start: 0, end: 2, slot: 0 },
    { run_index: 1, process_id: 'saw', start: 2, end: 4, slot: 0 },
  ],
  points: [
    { tick: 0, inventory: { wood: 2, plank: 0 }, busy_workers: 1, value: 2 },
    { tick: 2, inventory: { wood: 0, plank: 1 }, busy_workers: 1, value: 4 },
    { tick: 4, inventory: { wood: 0, plank: 2 }, busy_workers: 0, value: 8 },
  ],
};

describe('ResultsView', () => {
  let http: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [ResultsView],
      providers: [provideHttpClient(), provideHttpClientTesting()],
    });
    TestBed.overrideComponent(ResultsView, {
      remove: { imports: [ChartComponent] },
      add: { imports: [ChartStub] },
    });
    http = TestBed.inject(HttpTestingController);
  });

  async function render(sol: Solution | null) {
    const fixture = TestBed.createComponent(ResultsView);
    fixture.componentRef.setInput('instance', instance);
    fixture.componentRef.setInput('solution', sol);
    await fixture.whenStable();
    return { fixture, el: fixture.nativeElement as HTMLElement };
  }

  it('shows an empty state without a solution', async () => {
    const { el } = await render(null);
    expect(el.textContent).toContain('Run a solver to see its schedule here.');
    http.expectNone('/api/schedules/timeline');
  });

  it('summarizes the solution and draws the engine timeline', async () => {
    const { fixture, el } = await render(solution);
    const req = http.expectOne('/api/schedules/timeline');
    expect(req.request.body).toEqual({ instance, schedule: solution.runs });
    req.flush(timeline);
    await new Promise((r) => setTimeout(r));
    await fixture.whenStable();

    expect(el.querySelector('[data-testid=earnings]')?.textContent?.trim()).toBe('8');
    expect(el.textContent).toContain('+4'); // vs. baseline 4
    expect(el.textContent).toContain('proven optimal');
    expect(el.textContent).toContain('nodes');
    expect(el.querySelectorAll('app-gantt rect.bar')).toHaveLength(2);
    expect(el.querySelector('.chart-stub')?.textContent).toContain('Inventory');
  });

  it('shows why a solver failed', async () => {
    const { el } = await render({ ...solution, valid: false, error: 'DP state limit exceeded' });
    expect(el.querySelector('[role=alert]')?.textContent).toContain('DP state limit exceeded');
    http.expectNone('/api/schedules/timeline');
  });

  it('shows timeline errors', async () => {
    const { fixture, el } = await render(solution);
    http
      .expectOne('/api/schedules/timeline')
      .flush(
        { error: 'past_horizon', reason: 'saw would finish after the horizon' },
        { status: 422, statusText: 'Unprocessable' },
      );
    await new Promise((r) => setTimeout(r));
    await fixture.whenStable();
    expect(el.querySelector('[role=alert]')?.textContent).toContain(
      'saw would finish after the horizon',
    );
  });

  it('computes the do-nothing baseline', () => {
    expect(baselineEarnings({ ...instance, start_money: 3 })).toBe(7);
  });
});
