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
import { scaleLinear } from 'd3-scale';
import { pointer, select } from 'd3-selection';
import { area, line } from 'd3-shape';

import { HourReport, money } from './factory-models';

const W = 268;
const H = 92;
const PAD = { left: 4, right: 4, top: 8, bottom: 8 };

/**
 * Profit so far, hour by hour, as a D3 area chart with a marker on the current hour. Click
 * or drag on it to jump to an hour.
 */
@Component({
  selector: 'app-money-chart',
  changeDetection: ChangeDetectionStrategy.OnPush,
  encapsulation: ViewEncapsulation.None,
  template: `<svg
    #svg
    class="money-chart"
    [attr.viewBox]="'0 0 ' + width + ' ' + height"
    role="slider"
    tabindex="0"
    aria-label="Hour"
    [attr.aria-valuemin]="1"
    [attr.aria-valuemax]="hours().length"
    [attr.aria-valuenow]="hour() + 1"
    [attr.aria-valuetext]="'Hour ' + (hour() + 1) + ', ' + money(hours()[hour()]?.profit ?? 0)"
    (keydown)="onKey($event)"
  ></svg>`,
  styles: `
    .money-chart {
      display: block;
      width: 100%;
      height: auto;
      background: #2f6fd6;
      border: 2px solid #1f2430;
      border-radius: 10px;
      cursor: pointer;
      outline: none;
    }
    .money-chart:focus-visible {
      outline: 2px solid #1f2430;
    }
    .money-chart .area {
      fill: rgba(255, 255, 255, 0.28);
    }
    .money-chart .line {
      fill: none;
      stroke: #fff;
      stroke-width: 2;
    }
    .money-chart .zero {
      stroke: rgba(255, 255, 255, 0.55);
      stroke-dasharray: 3 3;
    }
    .money-chart .marker {
      stroke: #ffd54a;
      stroke-width: 2;
    }
    .money-chart .knob {
      fill: #ffd54a;
      stroke: #1f2430;
      stroke-width: 2;
    }
  `,
})
export class MoneyChart {
  readonly hours = input<HourReport[]>([]);
  readonly hour = input(0);
  readonly seek = output<number>();

  protected readonly width = W;
  protected readonly height = H;
  protected readonly money = money;
  private readonly svgRef = viewChild.required<ElementRef<SVGSVGElement>>('svg');
  private ready = false;

  constructor() {
    afterNextRender(() => {
      this.ready = true;
      const svg = select(this.svgRef().nativeElement);
      let dragging = false;
      const seekTo = (e: PointerEvent) => {
        const n = this.hours().length;
        if (!n) return;
        // d3's pointer() maps through the SVG's screen matrix, so x is in viewBox units.
        const [x] = pointer(e, this.svgRef().nativeElement);
        const t = (x - PAD.left) / (W - PAD.left - PAD.right);
        this.seek.emit(Math.round(Math.min(1, Math.max(0, t)) * (n - 1)));
      };
      svg
        .on('pointerdown', (e: PointerEvent) => {
          dragging = true;
          seekTo(e);
        })
        .on('pointermove', (e: PointerEvent) => dragging && seekTo(e))
        .on('pointerup pointerleave', () => (dragging = false));
      this.render();
    });
    effect(() => {
      this.hours();
      this.hour();
      if (this.ready) this.render();
    });
  }

  protected onKey(e: KeyboardEvent): void {
    const delta = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
    if (e.key === 'Home') this.seek.emit(0);
    else if (e.key === 'End') this.seek.emit(this.hours().length - 1);
    else if (delta) this.seek.emit(this.hour() + delta);
    else return;
    e.preventDefault();
  }

  private render(): void {
    const data = this.hours();
    const svg = select(this.svgRef().nativeElement);
    if (!data.length) {
      svg.selectAll('*').remove();
      return;
    }
    const values = data.map((h) => h.profit);
    const lo = Math.min(0, ...values);
    const hi = Math.max(0, ...values);
    const x = scaleLinear()
      .domain([0, Math.max(1, data.length - 1)])
      .range([PAD.left, W - PAD.right]);
    const y = scaleLinear()
      .domain([lo, hi === lo ? lo + 1 : hi])
      .range([H - PAD.bottom, PAD.top]);
    const shape = area<HourReport>()
      .x((d) => x(d.hour))
      .y0(y(0))
      .y1((d) => y(d.profit));
    const path = line<HourReport>()
      .x((d) => x(d.hour))
      .y((d) => y(d.profit));

    const ensure = (tag: string, cls: string) => {
      let el = svg.select<SVGElement>(`${tag}.${cls}`);
      if (el.empty()) el = svg.append<SVGElement>(tag).attr('class', cls);
      return el;
    };
    ensure('path', 'area').datum(data).attr('d', shape);
    ensure('path', 'line').datum(data).attr('d', path);
    ensure('line', 'zero')
      .attr('x1', PAD.left)
      .attr('x2', W - PAD.right)
      .attr('y1', y(0))
      .attr('y2', y(0));
    const cur = data[Math.min(this.hour(), data.length - 1)];
    ensure('line', 'marker')
      .attr('x1', x(cur.hour))
      .attr('x2', x(cur.hour))
      .attr('y1', PAD.top - 4)
      .attr('y2', H - PAD.bottom + 4);
    ensure('circle', 'knob').attr('r', 5).attr('cx', x(cur.hour)).attr('cy', y(cur.profit));
  }
}
