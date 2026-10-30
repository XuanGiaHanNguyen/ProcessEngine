import {
  ChangeDetectionStrategy,
  Component,
  DestroyRef,
  ElementRef,
  ViewEncapsulation,
  afterNextRender,
  effect,
  inject,
  input,
  output,
  viewChild,
} from '@angular/core';
import { scaleLinear } from 'd3-scale';
import { Selection, select } from 'd3-selection';
import { linkHorizontal } from 'd3-shape';
import 'd3-transition';
import { ZoomBehavior, zoom, zoomIdentity } from 'd3-zoom';

import { Design, FactoryLevel, money } from './factory-models';
import { iconFor, itemColor } from './icons';

type NodeKind = 'supplier' | 'machine' | 'slot' | 'market';

/** A drawable node: a supplier, a built machine, an empty slot, or the market. */
interface CanvasNode {
  id: string;
  kind: NodeKind;
  x: number;
  y: number;
  icon: string;
  title: string;
  badge: string;
  sub: string;
  active: boolean;
}

/** A drawable belt between two node centers. */
interface CanvasBelt {
  key: string;
  resource: string;
  source: [number, number];
  target: [number, number];
  active: boolean;
}

const UNIT_X = 64;
const UNIT_Y = 58;
const R = 30;

/**
 * The factory floor, drawn with D3: zoom and pan (d3-zoom), belts as horizontal links
 * (d3-shape), grid-to-pixel scales (d3-scale), and items flowing along belts as D3
 * transitions. Angular owns the data; D3 owns the SVG under the viewport group.
 */
@Component({
  selector: 'app-factory-canvas',
  changeDetection: ChangeDetectionStrategy.OnPush,
  // D3 creates the SVG children, so styles cannot rely on Angular's emulated encapsulation.
  encapsulation: ViewEncapsulation.None,
  template: `
    <svg
      #svg
      class="floor"
      role="img"
      [attr.aria-label]="'Factory floor of ' + (level()?.name ?? '')"
    >
      <defs>
        <pattern id="pf-dots" width="24" height="24" patternUnits="userSpaceOnUse">
          <circle cx="2" cy="2" r="1.2" fill="#3a3f4c" />
        </pattern>
      </defs>
      <rect class="bg" width="100%" height="100%" fill="url(#pf-dots)" />
      <g class="viewport"></g>
    </svg>
    <div class="zoom-controls">
      <button type="button" (click)="zoomBy(1.25)" aria-label="Zoom in">+</button>
      <button type="button" (click)="zoomBy(0.8)" aria-label="Zoom out">−</button>
      <button type="button" (click)="fit()" aria-label="Fit to screen">⤢</button>
    </div>
  `,
  styleUrl: './factory-canvas.scss',
})
export class FactoryCanvas {
  readonly level = input<FactoryLevel | null>(null);
  readonly design = input<Design | null>(null);
  readonly selected = input<string | null>(null);
  /** Animate items along belts. */
  readonly flowing = input(true);
  /** Pixels covered by overlays on the left and right, kept clear when fitting. */
  readonly insets = input<[number, number]>([0, 0]);
  readonly nodeSelected = output<string | null>();

  private readonly svgRef = viewChild.required<ElementRef<SVGSVGElement>>('svg');
  private zoomBehavior?: ZoomBehavior<SVGSVGElement, unknown>;
  private bounds = { w: 0, h: 0 };
  private flowTimer?: ReturnType<typeof setInterval>;
  private readonly reducedMotion =
    typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;

  constructor() {
    afterNextRender(() => {
      const svg = select(this.svgRef().nativeElement);
      this.zoomBehavior = zoom<SVGSVGElement, unknown>()
        .scaleExtent([0.35, 2.5])
        .on('zoom', (e) => svg.select('g.viewport').attr('transform', e.transform.toString()));
      svg.call(this.zoomBehavior).on('dblclick.zoom', null);
      svg.select('rect.bg').on('click', () => this.nodeSelected.emit(null));
      this.render();
      this.fit();
    });
    effect(() => {
      this.level();
      this.design();
      this.selected();
      if (this.zoomBehavior) this.render();
    });
    let lastLevel: string | undefined;
    effect(() => {
      const id = this.level()?.id;
      if (this.zoomBehavior && id !== lastLevel) {
        lastLevel = id;
        this.fit();
      }
    });
    effect(() => {
      clearInterval(this.flowTimer);
      if (this.flowing() && !this.reducedMotion) {
        this.flowTimer = setInterval(() => this.spawnItems(), 650);
      }
    });
    inject(DestroyRef).onDestroy(() => clearInterval(this.flowTimer));
  }

  /** Zooms around the center by `k`. */
  zoomBy(k: number): void {
    if (!this.zoomBehavior) return;
    select(this.svgRef().nativeElement)
      .transition()
      .duration(250)
      .call(this.zoomBehavior.scaleBy, k);
  }

  /** Fits the whole floor into view. */
  fit(): void {
    if (!this.zoomBehavior) return;
    const el = this.svgRef().nativeElement;
    const { width, height } = el.getBoundingClientRect();
    if (!width || !height || !this.bounds.w) return;
    // Leave room for the floating side panels on wide screens.
    const [left, right] = width >= 900 ? this.insets() : [0, 0];
    const usable = width - left - right;
    const pad = 40;
    const k = Math.min(1.3, (usable - pad) / this.bounds.w, (height - pad) / this.bounds.h);
    const t = zoomIdentity
      .translate(left + (usable - this.bounds.w * k) / 2, (height - this.bounds.h * k) / 2)
      .scale(k);
    select(el).transition().duration(400).call(this.zoomBehavior.transform, t);
  }

  private model(): { nodes: CanvasNode[]; belts: CanvasBelt[] } {
    const level = this.level();
    const design = this.design();
    if (!level) return { nodes: [], belts: [] };
    const maxX = Math.max(level.market.x, ...level.slots.map((s) => s.x));
    const maxY = Math.max(
      level.market.y,
      ...level.slots.map((s) => s.y),
      ...level.suppliers.map((s) => s.y),
    );
    const sx = scaleLinear()
      .domain([0, maxX])
      .range([R + 20, R + 20 + maxX * UNIT_X]);
    const sy = scaleLinear()
      .domain([0, maxY])
      .range([R + 10, R + 10 + maxY * UNIT_Y]);
    this.bounds = { w: sx(maxX) + R + 40, h: sy(maxY) + R + 50 };

    const nodes: CanvasNode[] = [];
    const orders = design?.orders ?? {};
    for (const s of level.suppliers) {
      const rate = orders[s.id] ?? 0;
      nodes.push({
        id: s.id,
        kind: 'supplier',
        x: sx(s.x),
        y: sy(s.y),
        icon: iconFor(s.resource),
        title: level.resources.find((r) => r.id === s.resource)?.name ?? s.resource,
        badge: `${rate}/h`,
        sub: `x${money(s.unit_cost)}`,
        active: rate > 0,
      });
    }
    const placed = new Map((design?.machines ?? []).map((m) => [m.slot, m]));
    for (const slot of level.slots) {
      const m = placed.get(slot.id);
      const type = m && level.machine_types.find((t) => t.id === m.type);
      const recipe = type?.recipes.find((r) => r.id === m?.recipe);
      if (m && type && recipe) {
        const out = Object.keys(recipe.outputs)[0];
        nodes.push({
          id: slot.id,
          kind: 'machine',
          x: sx(slot.x),
          y: sy(slot.y),
          icon: iconFor(out),
          title: `${type.name}: ${recipe.name}`,
          badge: type.name,
          sub: `${recipe.hours} hr${recipe.hours > 1 ? 's' : ''}`,
          active: true,
        });
      } else {
        nodes.push({
          id: slot.id,
          kind: 'slot',
          x: sx(slot.x),
          y: sy(slot.y),
          icon: '+',
          title: `Empty slot ${slot.id}`,
          badge: '',
          sub: '',
          active: false,
        });
      }
    }
    nodes.push({
      id: 'market',
      kind: 'market',
      x: sx(level.market.x),
      y: sy(level.market.y),
      icon: '🛒',
      title: 'Market',
      badge: 'Market',
      sub: '',
      active: true,
    });

    const byId = new Map(nodes.map((n) => [n.id, n]));
    const belts: CanvasBelt[] = [];
    for (const b of design?.belts ?? []) {
      const from = byId.get(b.from);
      const to = byId.get(b.to);
      if (!from || !to) continue;
      belts.push({
        key: `${b.from}>${b.to}:${b.resource}`,
        resource: b.resource,
        source: [from.x + R, from.y],
        target: [to.x - R, to.y],
        active: from.active,
      });
    }
    return { nodes, belts };
  }

  private render(): void {
    const { nodes, belts } = this.model();
    const vp = select(this.svgRef().nativeElement).select<SVGGElement>('g.viewport');
    const link = linkHorizontal<CanvasBelt, [number, number]>()
      .source((d) => d.source)
      .target((d) => d.target);

    const beltLayer = this.layer(vp, 'belts');
    const belt = beltLayer
      .selectAll<SVGGElement, CanvasBelt>('g.belt')
      .data(belts, (d) => d.key)
      .join((enter) => {
        const g = enter.append('g').attr('class', 'belt');
        g.append('path').attr('class', 'belt-edge');
        g.append('path').attr('class', 'belt-track');
        g.append('path').attr('class', 'belt-dash');
        g.append('title');
        return g;
      });
    belt.classed('idle', (d) => !d.active);
    belt.selectAll<SVGPathElement, CanvasBelt>('path').attr('d', (d) => link(d));
    belt.select('title').text((d) => d.key.replace('>', ' → ').replace(':', ' · '));

    this.layer(vp, 'items');

    const nodeLayer = this.layer(vp, 'nodes');
    const node = nodeLayer
      .selectAll<SVGGElement, CanvasNode>('g.node')
      .data(nodes, (d) => d.id)
      .join((enter) => {
        const g = enter.append('g').attr('tabindex', 0).attr('role', 'button');
        g.append('circle')
          .attr('class', 'gear')
          .attr('r', R + 4);
        g.append('circle').attr('class', 'disc').attr('r', R);
        g.append('text').attr('class', 'icon').attr('dy', '0.35em');
        const badge = g
          .append('g')
          .attr('class', 'node-badge')
          .attr('transform', `translate(0, ${R + 18})`);
        badge.append('rect').attr('rx', 7);
        badge.append('text').attr('class', 'badge-main').attr('dy', '0.35em');
        badge.append('text').attr('class', 'badge-sub').attr('dy', '0.35em');
        g.append('title');
        g.on('click', (e: MouseEvent, d) => {
          e.stopPropagation();
          this.nodeSelected.emit(d.id);
        });
        g.on('keydown', (e: KeyboardEvent, d) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            this.nodeSelected.emit(d.id);
          }
        });
        return g;
      });
    node
      .attr('class', (d) => `node ${d.kind}${d.active ? ' active' : ''}`)
      .classed('selected', (d) => d.id === this.selected())
      .attr('transform', (d) => `translate(${d.x}, ${d.y})`)
      .attr('aria-label', (d) => d.title);
    node.select('text.icon').text((d) => d.icon);
    node.select('title').text((d) => d.title);
    node.each(function (d) {
      const badge = select(this).select<SVGGElement>('g.node-badge');
      badge.attr('display', d.badge ? null : 'none');
      const main = badge.select<SVGTextElement>('text.badge-main').text(d.badge);
      const sub = badge.select<SVGTextElement>('text.badge-sub').text(d.sub);
      main.attr('y', d.sub ? -8 : 0);
      sub.attr('y', 9);
      // getComputedTextLength is missing in jsdom (unit tests); fall back to a rough width.
      const width = (t: SVGTextElement | null, text: string) =>
        t && typeof t.getComputedTextLength === 'function'
          ? t.getComputedTextLength()
          : text.length * 7;
      const w = Math.max(width(main.node(), d.badge), width(sub.node(), d.sub)) + 18;
      badge
        .select('rect')
        .attr('x', -w / 2)
        .attr('y', d.sub ? -18 : -11)
        .attr('width', w)
        .attr('height', d.sub ? 36 : 22);
    });
  }

  /** Sends one item along each active belt, as a D3 transition that follows the path. */
  private spawnItems(): void {
    const vp = select(this.svgRef().nativeElement).select<SVGGElement>('g.viewport');
    const items = vp.select<SVGGElement>('g.items');
    if (items.empty() || document.hidden) return;
    vp.selectAll<SVGGElement, CanvasBelt>('g.belt:not(.idle)').each(function (d) {
      const path = select(this).select<SVGPathElement>('path.belt-track').node();
      if (!path || typeof path.getTotalLength !== 'function') return;
      const len = path.getTotalLength();
      items
        .append('circle')
        .attr('class', 'item')
        .attr('r', 4.5)
        .attr('fill', itemColor(d.resource))
        .transition()
        .duration(Math.max(900, len * 9))
        .ease((t) => t)
        .attrTween('transform', () => (t: number) => {
          const p = path.getPointAtLength(t * len);
          return `translate(${p.x}, ${p.y})`;
        })
        .remove();
    });
  }

  private layer(
    vp: Selection<SVGGElement, unknown, null, undefined>,
    name: string,
  ): Selection<SVGGElement, unknown, null, undefined> {
    let g = vp.select<SVGGElement>(`g.${name}`);
    if (g.empty()) g = vp.append('g').attr('class', name);
    return g;
  }
}
