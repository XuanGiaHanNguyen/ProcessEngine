import { Component, computed, input } from '@angular/core';

/** A bar on the Gantt chart. */
export interface GanttBar {
  label: string;
  start: number;
  end: number;
  slot: number;
  color: string;
  /** Drawn hatched (e.g. a planned but not yet started run). */
  ghost?: boolean;
}

/**
 * SVG Gantt chart: one lane per worker slot, ticks on the x axis. Optionally marks the
 * current tick (Play mode). Bars carry <title> tooltips; the whole chart has a text summary.
 */
@Component({
  selector: 'app-gantt',
  template: `
    <svg
      [attr.viewBox]="'0 0 ' + width + ' ' + svgHeight()"
      preserveAspectRatio="none"
      role="img"
      [attr.aria-label]="summary()"
      [style.height.px]="svgHeight()"
    >
      @for (lane of lanes(); track lane) {
        <rect class="lane" [attr.x]="left" [attr.y]="laneY(lane)" [attr.width]="plotWidth" [attr.height]="laneHeight - 4" />
        <text class="lane-label" x="4" [attr.y]="laneY(lane) + laneHeight / 2 + 2">W{{ lane + 1 }}</text>
      }
      @for (t of ticks(); track t) {
        <line class="grid" [attr.x1]="x(t)" [attr.x2]="x(t)" y1="0" [attr.y2]="lanesHeight()" />
        <text class="tick" [attr.x]="x(t)" [attr.y]="lanesHeight() + 14">{{ t }}</text>
      }
      @for (bar of bars(); track $index) {
        <g>
          <title>{{ bar.label }}: ticks {{ bar.start }}–{{ bar.end }}</title>
          <rect
            class="bar"
            [class.ghost]="bar.ghost"
            [attr.x]="x(bar.start) + 1"
            [attr.y]="laneY(bar.slot) + 2"
            [attr.width]="max(2, x(bar.end) - x(bar.start) - 2)"
            [attr.height]="laneHeight - 8"
            [attr.fill]="bar.ghost ? 'none' : bar.color"
            [attr.stroke]="bar.color"
            rx="3"
          />
          @if (x(bar.end) - x(bar.start) > 34) {
            <text class="bar-label" [class.ghost]="bar.ghost" [attr.x]="x(bar.start) + 5" [attr.y]="laneY(bar.slot) + laneHeight / 2 + 2">
              {{ bar.label }}
            </text>
          }
        </g>
      }
      @if (currentTick() !== null) {
        <line class="now" [attr.x1]="x(currentTick()!)" [attr.x2]="x(currentTick()!)" y1="0" [attr.y2]="lanesHeight()" />
      }
    </svg>
  `,
  styles: `
    svg {
      width: 100%;
      display: block;
    }
    .lane {
      fill: #f3f5f9;
    }
    .lane-label,
    .tick {
      font-size: 10px;
      fill: #556074;
    }
    .tick {
      text-anchor: middle;
    }
    .grid {
      stroke: #e2e7ef;
      stroke-width: 1;
    }
    .bar {
      stroke-width: 1.5;
    }
    .bar.ghost {
      stroke-dasharray: 4 3;
    }
    .bar-label {
      font-size: 10px;
      fill: #fff;
      font-weight: 600;
      pointer-events: none;
    }
    .bar-label.ghost {
      fill: #1e2533;
    }
    .now {
      stroke: #b42318;
      stroke-width: 2;
    }
  `,
})
export class GanttComponent {
  readonly bars = input.required<GanttBar[]>();
  readonly workers = input.required<number>();
  readonly horizon = input.required<number>();
  readonly currentTick = input<number | null>(null);

  protected readonly width = 1000;
  protected readonly left = 28;
  protected readonly plotWidth = this.width - this.left - 8;
  protected readonly laneHeight = 30;
  protected readonly max = Math.max;

  protected readonly lanes = computed(() => Array.from({ length: this.workers() }, (_, i) => i));
  protected readonly lanesHeight = computed(() => this.workers() * this.laneHeight);
  protected readonly svgHeight = computed(() => this.lanesHeight() + 20);
  protected readonly ticks = computed(() => {
    const h = Math.max(1, this.horizon());
    const step = Math.max(1, Math.ceil(h / 20));
    const out: number[] = [];
    for (let t = 0; t <= h; t += step) out.push(t);
    if (out[out.length - 1] !== h) out.push(h);
    return out;
  });
  protected readonly summary = computed(
    () =>
      `Timeline of ${this.bars().length} runs on ${this.workers()} worker slots over ${this.horizon()} ticks.`,
  );

  protected x(tick: number): number {
    return this.left + (tick / Math.max(1, this.horizon())) * this.plotWidth;
  }
  protected laneY(slot: number): number {
    return slot * this.laneHeight;
  }
}
