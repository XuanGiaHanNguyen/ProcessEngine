import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';

import { ComparisonChart, ComparisonRow } from './comparison-chart';
import { HourReport, MachineStatus } from './factory-models';
import { MachineTimeline } from './machine-timeline';

function hour(i: number, statuses: MachineStatus[]): HourReport {
  return {
    hour: i,
    revenue: 0,
    purchases: 0,
    profit: 0,
    sold: {},
    belts: [],
    stock: {},
    machines: statuses.map((status, m) => ({
      slot: `s${m + 1}`,
      status,
      progress: 0,
      total: 1,
      missing: [],
      inputs: {},
      outputs: {},
      batches_done: 0,
    })),
  };
}

@Component({
  imports: [ComparisonChart, MachineTimeline],
  template: `
    <app-comparison-chart
      [rows]="rows()"
      [target]="5000"
      [bound]="9000"
      (pick)="picked.set($event)"
    />
    <app-machine-timeline
      [hours]="hours"
      [labels]="['Loom s1', 'Carder s2']"
      [hour]="1"
      (seek)="sought.set($event)"
    />
  `,
})
class Host {
  readonly rows = signal<ComparisonRow[]>([
    { id: 1, label: 'Initial', profit: 1000, kind: 'initial' },
    { id: 2, label: '🤖 greedy', profit: 4000, kind: 'solver' },
    { id: 3, label: 'Test 1', profit: -200, kind: 'player' },
  ]);
  readonly hours = [
    hour(0, ['busy', 'starved']),
    hour(1, ['blocked', 'busy']),
    hour(2, ['busy', 'busy']),
  ];
  readonly picked = signal<number | null>(null);
  readonly sought = signal<number | null>(null);
}

describe('factory charts', () => {
  async function render() {
    const fixture = TestBed.createComponent(Host);
    fixture.detectChanges();
    await fixture.whenStable();
    return fixture;
  }

  it('draws one bar per tested factory, with the best known and the bound', async () => {
    const fixture = await render();
    const el = fixture.nativeElement as HTMLElement;
    expect(el.querySelectorAll('.comparison-chart g.bar')).toHaveLength(3);
    expect(el.querySelector('.comparison-chart g.bar.player')).not.toBeNull();
    expect(
      [...el.querySelectorAll('.comparison-chart .ref text')].map((t) => t.textContent),
    ).toEqual(['best known', 'upper bound']);
    el.querySelectorAll('.comparison-chart g.bar')[1].dispatchEvent(
      new MouseEvent('click', { bubbles: true }),
    );
    expect(fixture.componentInstance.picked()).toBe(2);
  });

  it('draws each machine hour by status and seeks on click', async () => {
    const fixture = await render();
    const el = fixture.nativeElement as HTMLElement;
    expect(el.querySelectorAll('.machine-timeline rect.cell')).toHaveLength(6);
    expect(el.querySelectorAll('.machine-timeline rect.cell.blocked')).toHaveLength(1);
    expect(el.querySelectorAll('.machine-timeline rect.now')).toHaveLength(1);
    el.querySelectorAll('.machine-timeline rect.cell')[2].dispatchEvent(
      new MouseEvent('click', { bubbles: true }),
    );
    expect(fixture.componentInstance.sought()).toBe(2);
  });
});
