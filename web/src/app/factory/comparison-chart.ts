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
import { scaleBand, scaleLinear } from 'd3-scale';
import { select } from 'd3-selection';
import 'd3-transition';

import { money } from './factory-models';

/** One bar: a tested factory and its profit. */
export interface ComparisonRow {
  id: number;
  label: string;
  profit: number;
  kind: 'initial' | 'player' | 'solver';
}

const W = 256;
const BAR = 18;
const GAP = 6;
const LABEL_W = 92;
const PAD = { top: 6, right: 8, bottom: 20 };

/**
 * Profits of tested factories as D3 horizontal bars, with the best known profit and the
 * upper bound as reference lines. Click a bar to play that factory back.
 */
@Component({
  selector: 'app-comparison-chart',
  changeDetection: ChangeDetectionStrategy.OnPush,
  encapsulation: ViewEncapsulation.None,
  template: `<svg
    #svg
    class="comparison-chart"
    role="img"
    aria-label="Profit of each factory"
  ></svg>`,
  styles: `
    .comparison-chart {
      display: block;
      width: 100%;
      height: auto;
    }
    .comparison-chart .bar {
      cursor: pointer;
    }
    .comparison-chart .bar rect {
      rx: 4px;
    }
    .comparison-chart .bar.player rect {
      fill: #3fae6a;
    }
    .comparison-chart .bar.solver rect {
      fill: #7fb0ff;
    }
    .comparison-chart .bar.initial rect {
      fill: #8b90a0;
    }
    .comparison-chart .bar.active rect {
      stroke: #fff;
      stroke-width: 2;
    }
    .comparison-chart .bar:hover rect {
      filter: brightness(1.2);
    }
    .comparison-chart text {
      fill: #f3f4f7;
      font-size: 10.5px;
    }
    .comparison-chart .value {
      font-weight: 700;
    }
    .comparison-chart .ref line {
      stroke-width: 1.5;
      stroke-dasharray: 4 3;
    }
    .comparison-chart .ref.target line {
      stroke: #ffd447;
    }
    .comparison-chart .ref.bound line {
      stroke: #ff9b9b;
    }
    .comparison-chart .ref text {
      font-size: 9.5px;
      text-anchor: middle;
    }
  `,
})
export class ComparisonChart {
  readonly rows = input<ComparisonRow[]>([]);
  readonly active = input<number | null>(null);
  /** Best known profit of the level. */
  readonly target = input<number | null>(null);
  /** No design can earn more than this. */
  readonly bound = input<number | null>(null);
  readonly pick = output<number>();

  private readonly svgRef = viewChild.required<ElementRef<SVGSVGElement>>('svg');
  private ready = false;

  constructor() {
    afterNextRender(() => {
      this.ready = true;
      this.render();
    });
    effect(() => {
      this.rows();
      this.active();
      this.target();
      this.bound();
      if (this.ready) this.render();
    });
  }

  private render(): void {
    const rows = this.rows();
    const refs = [
      { kind: 'target', label: 'best known', value: this.target() },
      { kind: 'bound', label: 'upper bound', value: this.bound() },
    ].filter((r): r is { kind: string; label: string; value: number } => r.value !== null);
    const h = PAD.top + rows.length * (BAR + GAP) + PAD.bottom;
    const svg = select(this.svgRef().nativeElement).attr('viewBox', `0 0 ${W} ${h}`);
    const lo = Math.min(0, ...rows.map((r) => r.profit));
    const hi = Math.max(1, ...rows.map((r) => r.profit), ...refs.map((r) => r.value));
    const x = scaleLinear()
      .domain([lo, hi])
      .range([LABEL_W, W - PAD.right]);
    const y = scaleBand<number>()
      .domain(rows.map((r) => r.id))
      .range([PAD.top, PAD.top + rows.length * (BAR + GAP)])
      .paddingInner(GAP / (BAR + GAP));

    const bar = svg
      .selectAll<SVGGElement, ComparisonRow>('g.bar')
      .data(rows, (d) => d.id)
      .join((enter) => {
        const g = enter.append('g');
        g.append('text').attr('class', 'label').attr('x', 0).attr('dy', '0.35em');
        g.append('rect').attr('x', x(0)).attr('width', 0);
        g.append('text').attr('class', 'value').attr('dy', '0.35em');
        g.append('title');
        g.on('click', (_e, d) => this.pick.emit(d.id));
        return g;
      })
      .attr('class', (d) => `bar ${d.kind}${d.id === this.active() ? ' active' : ''}`)
      .attr('transform', (d) => `translate(0, ${y(d.id) ?? 0})`);
    bar
      .select('text.label')
      .attr('y', y.bandwidth() / 2)
      .text((d) => (d.label.length > 14 ? d.label.slice(0, 13) + '…' : d.label));
    bar
      .select('rect')
      .attr('height', y.bandwidth())
      .transition()
      .duration(400)
      .attr('x', (d) => x(Math.min(0, d.profit)))
      .attr('width', (d) => Math.max(1, Math.abs(x(d.profit) - x(0))));
    bar
      .select('text.value')
      .attr('y', y.bandwidth() / 2)
      .attr('x', (d) => Math.min(x(Math.max(0, d.profit)) + 4, W - 58))
      .text((d) => money(d.profit));
    bar.select('title').text((d) => `${d.label}: ${money(d.profit)}`);

    const ref = svg
      .selectAll<SVGGElement, (typeof refs)[number]>('g.ref')
      .data(refs, (d) => d.kind)
      .join((enter) => {
        const g = enter.append('g');
        g.append('line');
        g.append('text');
        return g;
      })
      .attr('class', (d) => `ref ${d.kind}`);
    ref
      .select('line')
      .attr('x1', (d) => x(d.value))
      .attr('x2', (d) => x(d.value))
      .attr('y1', PAD.top - 2)
      .attr('y2', h - PAD.bottom + 2);
    ref
      .select('text')
      .attr('x', (d) => Math.min(Math.max(x(d.value), LABEL_W + 24), W - 30))
      .attr('y', (d) => h - (d.kind === 'target' ? 9 : 1))
      .text((d) => d.label);
  }
}
