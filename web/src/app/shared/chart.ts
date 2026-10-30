import {
  Component,
  ElementRef,
  OnDestroy,
  afterRenderEffect,
  input,
  viewChild,
} from '@angular/core';
import { Chart, ChartConfiguration, registerables } from 'chart.js';

Chart.register(...registerables);

/**
 * Thin Chart.js wrapper. Re-renders whenever `config` changes. `label` is the accessible
 * name of the chart image; pass a short summary so screen-reader users get the gist.
 */
@Component({
  selector: 'app-chart',
  template: `<div class="wrap" [style.height.px]="height()">
    <canvas #canvas role="img" [attr.aria-label]="label()"></canvas>
  </div>`,
  styles: `
    .wrap {
      position: relative;
      width: 100%;
    }
  `,
})
export class ChartComponent implements OnDestroy {
  readonly config = input.required<ChartConfiguration>();
  readonly label = input.required<string>();
  readonly height = input(240);

  private readonly canvas = viewChild.required<ElementRef<HTMLCanvasElement>>('canvas');
  private chart?: Chart;

  constructor() {
    afterRenderEffect(() => {
      const config = this.config();
      this.chart?.destroy();
      this.chart = new Chart(this.canvas().nativeElement, {
        ...config,
        options: {
          responsive: true,
          maintainAspectRatio: false,
          animation: false,
          ...config.options,
        },
      });
    });
  }

  ngOnDestroy(): void {
    this.chart?.destroy();
  }
}
