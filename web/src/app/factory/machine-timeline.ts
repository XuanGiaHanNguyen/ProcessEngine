import {
  ChangeDetectionStrategy,
  Component,
  ElementRef,
  ViewEncapsulation,
  afterNextRender,
  effect,
  input,
  output,
  viewChild,
} from '@angular/core';
import { scaleBand } from 'd3-scale';
import { select } from 'd3-selection';

import { HourReport, MachineStatus } from './factory-models';

const W = 256;
const LABEL_W = 70;
const ROW = 14;
const PAD = { top: 4, bottom: 16 };

const STATUS_TEXT: Record<MachineStatus, string> = {
  busy: 'working',
  blocked: 'blocked',
  starved: 'waiting',
};

/**
 * What each machine did in each hour of a test, as a D3 grid: green working, amber waiting
 * for input, red blocked. The current hour is outlined; click a cell to jump to its hour.
 */
@Component({
  selector: 'app-machine-timeline',
  changeDetection: ChangeDetectionStrategy.OnPush,
  encapsulation: ViewEncapsulation.None,
  template: `<svg #svg class="machine-timeline" role="img" aria-label="Machine timeline"></svg>`,
  styles: `
    .machine-timeline {
      display: block;
      width: 100%;
      height: auto;
    }
    .machine-timeline .cell {
      cursor: pointer;
    }
    .machine-timeline .cell.busy {
      fill: #3fae6a;
    }
    .machine-timeline .cell.starved {
      fill: #a8892f;
    }
    .machine-timeline .cell.blocked {
      fill: #d9534f;
    }
    .machine-timeline .cell:hover {
      filter: brightness(1.3);
    }
    .machine-timeline text {
      fill: #1f2430;
      font-size: 10px;
    }
    .machine-timeline .now {
      fill: none;
      stroke: #1f2430;
      stroke-width: 1.5;
      pointer-events: none;
    }
  `,
})
export class MachineTimeline {
  readonly hours = input<HourReport[]>([]);
  /** One label per machine, in design order. */
  readonly labels = input<string[]>([]);
  readonly hour = input(0);
  readonly seek = output<number>();

  private readonly svgRef = viewChild.required<ElementRef<SVGSVGElement>>('svg');
  private ready = false;

  constructor() {
    afterNextRender(() => {
      this.ready = true;
      this.render();
    });
    effect(() => {
      this.hours();
      this.labels();
      this.hour();
      if (this.ready) this.render();
    });
  }

  private render(): void {
    const hours = this.hours();
    const labels = this.labels();
    const h = PAD.top + labels.length * ROW + PAD.bottom;
    const svg = select(this.svgRef().nativeElement).attr('viewBox', `0 0 ${W} ${h}`);
    const x = scaleBand<number>()
      .domain(hours.map((_, i) => i))
      .range([LABEL_W, W])
      .paddingInner(0.12);
    const cells = labels.flatMap((label, m) =>
      hours.map((hr, i) => ({ key: `${m}:${i}`, m, i, label, status: hr.machines[m]?.status })),
    );

    svg
      .selectAll<SVGTextElement, string>('text.row')
      .data(labels)
      .join('text')
      .attr('class', 'row')
      .attr('x', 0)
      .attr('y', (_, m) => PAD.top + m * ROW + ROW / 2)
      .attr('dy', '0.35em')
      .text((d) => (d.length > 12 ? d.slice(0, 11) + '…' : d));

    svg
      .selectAll<SVGRectElement, (typeof cells)[number]>('rect.cell')
      .data(cells, (d) => d.key)
      .join((enter) => {
        const r = enter.append('rect');
        r.append('title');
        r.on('click', (_e, d) => this.seek.emit(d.i));
        return r;
      })
      .attr('class', (d) => `cell ${d.status ?? ''}`)
      .attr('x', (d) => x(d.i) ?? 0)
      .attr('y', (d) => PAD.top + d.m * ROW + 1)
      .attr('width', x.bandwidth())
      .attr('height', ROW - 2)
      .select('title')
      .text((d) => `${d.label}, hour ${d.i + 1}: ${d.status ? STATUS_TEXT[d.status] : ''}`);

    svg
      .selectAll<SVGRectElement, number>('rect.now')
      .data(hours.length ? [this.hour()] : [])
      .join('rect')
      .attr('class', 'now')
      .attr('x', (d) => (x(d) ?? 0) - 1)
      .attr('y', PAD.top - 1)
      .attr('width', x.bandwidth() + 2)
      .attr('height', labels.length * ROW + 1);

    svg
      .selectAll<SVGTextElement, number>('text.tick')
      .data(hours.length ? [0, Math.floor(hours.length / 2), hours.length - 1] : [])
      .join('text')
      .attr('class', 'tick')
      .attr('x', (d) => (d === hours.length - 1 ? W : (x(d) ?? 0) + x.bandwidth() / 2))
      .attr('y', h - 4)
      .attr('text-anchor', (d) => (d === hours.length - 1 ? 'end' : 'middle'))
      .text((d) => `h${d + 1}`);
  }
}
