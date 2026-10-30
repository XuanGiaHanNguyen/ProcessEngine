import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';

import { FactoryCanvas } from './factory-canvas';
import { Design, FactoryLevel, money } from './factory-models';
import { tinyLevel } from './testing';

@Component({
  imports: [FactoryCanvas],
  template: `<app-factory-canvas
    [level]="level()"
    [design]="design()"
    [flowing]="false"
    (nodeSelected)="picked.set($event)"
  />`,
})
class Host {
  readonly level = signal<FactoryLevel | null>(tinyLevel());
  readonly design = signal<Design | null>(tinyLevel().reference_designs[0].design);
  readonly picked = signal<string | null>(null);
}

describe('FactoryCanvas', () => {
  async function render() {
    const fixture = TestBed.createComponent(Host);
    fixture.detectChanges();
    await fixture.whenStable();
    return fixture;
  }

  it('draws suppliers, machines, empty slots, the market and belts with D3', async () => {
    const fixture = await render();
    const el = fixture.nativeElement as HTMLElement;
    expect(el.querySelectorAll('g.node.supplier')).toHaveLength(1);
    expect(el.querySelectorAll('g.node.machine')).toHaveLength(1);
    expect(el.querySelectorAll('g.node.slot')).toHaveLength(1);
    expect(el.querySelectorAll('g.node.market')).toHaveLength(1);
    expect(el.querySelectorAll('g.belt')).toHaveLength(2);
    expect(el.querySelector('g.node.supplier .badge-main')?.textContent).toBe('3/h');
    expect(el.querySelector('g.node.machine')?.getAttribute('aria-label')).toBe(
      'Carder: Card stuffing',
    );
  });

  it('updates when the design changes and reports clicks', async () => {
    const fixture = await render();
    const host = fixture.componentInstance;
    host.design.set({ orders: {}, machines: [], belts: [] });
    fixture.detectChanges();
    await fixture.whenStable();
    const el = fixture.nativeElement as HTMLElement;
    expect(el.querySelectorAll('g.node.slot')).toHaveLength(2);
    expect(el.querySelectorAll('g.belt')).toHaveLength(0);
    el.querySelector<SVGGElement>('g.node.market')?.dispatchEvent(
      new MouseEvent('click', { bubbles: true }),
    );
    expect(host.picked()).toBe('market');
  });
});

describe('money', () => {
  it('formats cents as dollars', () => {
    expect(money(1620)).toBe('$16.20');
    expect(money(-305)).toBe('-$3.05');
    expect(money(0)).toBe('$0.00');
  });
});
