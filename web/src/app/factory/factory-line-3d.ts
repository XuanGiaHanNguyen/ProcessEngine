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
  signal,
  untracked,
} from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import { lucideMaximize, lucideMinus, lucidePlus } from '@ng-icons/lucide';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { CSS2DObject, CSS2DRenderer } from 'three/addons/renderers/CSS2DRenderer.js';

import {
  CORNER,
  FloorBelt,
  FloorNode,
  FloorSelection,
  FloorLayout,
  FLOOR_3D,
  R,
  layoutFloor,
} from './floor-layout';
import {
  Gauge,
  Kit,
  MachineLive,
  Rect,
  canvasTexture,
  gaugeModel,
  hallModel,
  machineModel,
  marketModel,
  shade,
  supplierModel,
  truckModel,
} from './line-3d-models';
import { Design, FactoryLevel, HourReport } from './factory-models';
import { MARKET_ICON, iconFor, itemColor } from './icons';
import { itemSign } from './line-3d-items';

/** World units per floor pixel of the 2D layout. */
const S = 1 / 20;
/** Height of a belt's surface. */
const BELT_Y = 0.72;
const BELT_W = 0.56;
/** Speed of the belts, in world units per second. */
const SPEED = 1.5;
/** Length of one chevron on a belt. */
const TILE = 0.9;
/** Distance between the items riding a belt. */
const ITEM_GAP = 2.2;
/** How high a belt rises to bridge over another, and the flat top and ramps of a bridge. */
const BRIDGE_H = 0.95;
const BRIDGE_TOP = 0.75;
const BRIDGE_RAMP = 2.0;
/** A color per machine type, in level order: one each for up to 12 types. */
const MACHINE_COLORS = [
  '#2f7de1',
  '#e2453c',
  '#36b55a',
  '#f29a1d',
  '#8e5cd9',
  '#1fb5b0',
  '#f2c230',
  '#e85a9b',
  '#9a6a3f',
  '#8cc63f',
  '#3b4fb8',
  '#ff7f5c',
];
const LAMP: Record<string, string> = {
  busy: '#3ddc5f',
  blocked: '#ff4d4d',
  starved: '#ffb020',
};

type Pick = { kind: 'node'; id: string } | { kind: 'belt'; index: number };

/** A node on the floor, and the parts of it that change with the hour or the selection. */
interface NodePart {
  node: FloorNode;
  group: THREE.Group;
  /** Where its name label goes when it is selected. */
  labelAt: THREE.Vector3;
  icon: string;
  machine?: MachineLive;
  gauge?: Gauge;
  faded: boolean;
  /** Its element over the canvas: a button to select it with the keyboard or drag a belt
   * from, named after it, with its badge lines as text for screen readers. */
  el: HTMLElement;
  badge: HTMLElement;
  sub: HTMLElement;
}

/** A conveyor that runs or stands still, and is highlighted or faded by the selection. */
interface Conveyor {
  group: THREE.Group;
  set(running: boolean): void;
  selected: boolean;
  faded: boolean;
}

/** The items riding one belt: a run of instances in the shared item meshes. */
interface ItemRun {
  line: Line;
  first: number;
  n: number;
  color: string;
  show: boolean;
}

/** A belt, or a supplier's feed from its truck, and the parts of it that change. */
interface BeltPart {
  belt?: FloorBelt;
  group: THREE.Group;
  line: Line;
  /** Where its legs stand and how tall they are. */
  legs: THREE.Vector3[];
  conveyor: Conveyor;
  items: ItemRun;
}

/** What a selection keeps in focus: its chain of belts and the nodes they join. */
interface Focus {
  nodes: Set<string>;
  belts: Set<string>;
}

/** A belt's centerline in world units, with the distance along it at each point and how
 * far it is lifted there to bridge over other belts. */
interface Line {
  pts: THREE.Vector2[];
  dist: number[];
  lift: number[];
  length: number;
}

/** The 2D floor's belt, with its rounded turns, as a line in world units. */
function beltLine(points: [number, number][]): Line {
  const p = points
    .map(([x, y]) => new THREE.Vector2(x * S, y * S))
    .filter((q, i, all) => i === 0 || q.distanceTo(all[i - 1]) > 0.02);
  const pts = [p[0].clone()];
  const corner = CORNER * S;
  for (let i = 1; i < p.length - 1; i++) {
    const [a, b, c] = [p[i - 1], p[i], p[i + 1]];
    const l1 = b.distanceTo(a);
    const l2 = c.distanceTo(b);
    // The same turns as the 2D floor: a run between two turns gives each turn half of it.
    const r = Math.min(corner, i === 1 ? l1 : l1 / 2, i === p.length - 2 ? l2 : l2 / 2);
    const u = b.clone().sub(a).divideScalar(l1);
    const v = c.clone().sub(b).divideScalar(l2);
    const k = r * 0.45;
    const curve = new THREE.CubicBezierCurve(
      b.clone().addScaledVector(u, -r),
      b.clone().addScaledVector(u, -k),
      b.clone().addScaledVector(v, k),
      b.clone().addScaledVector(v, r),
    );
    pts.push(...curve.getPoints(10));
  }
  pts.push(p[p.length - 1].clone());
  const line = pts.filter((q, i) => i === 0 || q.distanceTo(pts[i - 1]) > 0.01);
  const dist = [0];
  for (let i = 1; i < line.length; i++) dist.push(dist[i - 1] + line[i].distanceTo(line[i - 1]));
  return resample({ pts: line, dist, lift: [], length: dist[dist.length - 1] });
}

/** The line with points every `step` along it, so it can bend smoothly over bridges. */
function resample(line: Line, step = 0.2): Line {
  const n = Math.max(1, Math.ceil(line.length / step));
  const pts: THREE.Vector2[] = [];
  const dist: number[] = [];
  for (let i = 0; i <= n; i++) {
    const d = (i / n) * line.length;
    pts.push(pointAt(line, d).p);
    dist.push(d);
  }
  return { pts, dist, lift: pts.map(() => 0), length: line.length };
}

/** The line without the points it does not need: those on straight, level stretches. */
function simplify(line: Line): Line {
  const { pts, lift } = line;
  const keep = [0];
  for (let i = 1; i < pts.length - 1; i++) {
    const [a, b, c] = [pts[i - 1], pts[i], pts[i + 1]];
    const turn = Math.abs((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x));
    const bend = Math.abs(lift[i + 1] - 2 * lift[i] + lift[i - 1]);
    if (turn > 1e-5 || bend > 1e-5) keep.push(i);
  }
  keep.push(pts.length - 1);
  return {
    pts: keep.map((i) => pts[i]),
    dist: keep.map((i) => line.dist[i]),
    lift: keep.map((i) => lift[i]),
    length: line.length,
  };
}

/** Where two straight runs cross, if they do, away from their ends. */
function crossing(
  a0: THREE.Vector2,
  a1: THREE.Vector2,
  b0: THREE.Vector2,
  b1: THREE.Vector2,
): THREE.Vector2 | null {
  const r = a1.clone().sub(a0);
  const q = b1.clone().sub(b0);
  const den = r.cross(q);
  if (Math.abs(den) < 1e-6) return null;
  const w = b0.clone().sub(a0);
  const t = w.cross(q) / den;
  const u = w.cross(r) / den;
  return t > 0 && t < 1 && u > 0 && u < 1 ? a0.clone().addScaledVector(r, t) : null;
}

/**
 * Raises belts into bridges where they cross: at each crossing, the belt running front to
 * back goes over the one running across. A belt crossing several belts close together
 * stays up on one flat deck over all of them, with straight ramps at each end.
 * `corners` are each belt's corner points.
 */
function bridge(lines: Line[], corners: THREE.Vector2[][]): void {
  const along = (line: Line, at: THREE.Vector2) => {
    let best = 0;
    line.pts.forEach((p, i) => {
      if (p.distanceToSquared(at) < line.pts[best].distanceToSquared(at)) best = i;
    });
    return line.dist[best];
  };
  const crossings = new Map<Line, number[]>();
  for (let i = 0; i < lines.length; i++)
    for (let j = i + 1; j < lines.length; j++)
      for (let a = 1; a < corners[i].length; a++)
        for (let b = 1; b < corners[j].length; b++) {
          const [a0, a1, b0, b1] = [
            corners[i][a - 1],
            corners[i][a],
            corners[j][b - 1],
            corners[j][b],
          ];
          const at = crossing(a0, a1, b0, b1);
          if (!at) continue;
          const steep = (p: THREE.Vector2, q: THREE.Vector2) =>
            Math.abs(q.y - p.y) - Math.abs(q.x - p.x);
          const up = steep(a0, a1) >= steep(b0, b1) ? lines[i] : lines[j];
          const d = along(up, at);
          if (d < 0.8 || d > up.length - 0.8) continue;
          crossings.set(up, [...(crossings.get(up) ?? []), d]);
        }
  for (const [line, ds] of crossings) {
    // Decks: the stretch over each crossing, merged with the next one when the belt would
    // barely touch down between them.
    const decks: [number, number][] = [];
    for (const d of ds.sort((a, b) => a - b)) {
      const last = decks[decks.length - 1];
      if (last && d - BRIDGE_TOP - last[1] < 2 * BRIDGE_RAMP + 2) last[1] = d + BRIDGE_TOP;
      else decks.push([d - BRIDGE_TOP, d + BRIDGE_TOP]);
    }
    line.dist.forEach((x, k) => {
      for (const [from, to] of decks) {
        const off = Math.max(from - x, x - to, 0);
        line.lift[k] = Math.max(line.lift[k], BRIDGE_H * Math.max(0, 1 - off / BRIDGE_RAMP));
      }
    });
  }
}

/** The point `d` along a line, the direction it runs there, and how far it is lifted. */
function pointAt(line: Line, d: number): { p: THREE.Vector2; angle: number; lift: number } {
  let i = 1;
  while (i < line.dist.length - 1 && line.dist[i] < d) i++;
  const [a, b] = [line.pts[i - 1], line.pts[i]];
  const seg = line.dist[i] - line.dist[i - 1] || 1;
  const t = Math.min(1, Math.max(0, (d - line.dist[i - 1]) / seg));
  const lift = line.lift.length ? line.lift[i - 1] + (line.lift[i] - line.lift[i - 1]) * t : 0;
  return { p: a.clone().lerp(b, t), angle: Math.atan2(b.y - a.y, b.x - a.x), lift };
}

/**
 * Sweeps a cross-section along a line. The profile is [sideways offset, height] points,
 * going over the top from the left of the line (+) to its right (−), so faces point out.
 */
function sweep(line: Line, profile: [number, number][], closed = true): THREE.BufferGeometry {
  const pos: number[] = [];
  const uv: number[] = [];
  const idx: number[] = [];
  const n = line.pts.length;
  const side = line.pts.map((_, i) => {
    const a = line.pts[Math.max(0, i - 1)];
    const b = line.pts[Math.min(n - 1, i + 1)];
    const t = b.clone().sub(a).normalize();
    return new THREE.Vector2(-t.y, t.x);
  });
  const edges = closed ? profile.length : profile.length - 1;
  for (let e = 0; e < edges; e++) {
    const [oa, ha] = profile[e];
    const [ob, hb] = profile[(e + 1) % profile.length];
    const base = pos.length / 3;
    for (let i = 0; i < n; i++) {
      const [p, q] = [line.pts[i], side[i]];
      const y = line.lift[i] ?? 0;
      pos.push(p.x + q.x * oa, ha + y, p.y + q.y * oa, p.x + q.x * ob, hb + y, p.y + q.y * ob);
      uv.push(line.dist[i] / TILE, 0, line.dist[i] / TILE, 1);
    }
    for (let i = 0; i < n - 1; i++) {
      const a = base + 2 * i;
      idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

const at = <T extends THREE.Object3D>(o: T, x: number, y: number, z: number): T => {
  o.position.set(x, y, z);
  return o;
};

/** A rectangular cross-section from sideways offset `o0` to `o1`, height `y0` to `y1`. */
const rect = (o0: number, o1: number, y0: number, y1: number): [number, number][] => [
  [o1, y1],
  [o0, y1],
  [o0, y0],
  [o1, y0],
];

/** Fills a label line with an icon (an SVG string), if any, and its text. */
function setLine(line: HTMLElement, icon: string | undefined, text: string): void {
  line.innerHTML = text && icon ? icon : '';
  if (!text) return;
  const t = document.createElement('span');
  t.textContent = text;
  line.append(t);
}

/**
 * The factory floor in 3D: a toy factory hall with the line's suppliers, machines, belts and
 * market. Drag to orbit, right-drag to pan, scroll to zoom, click a machine or belt to
 * select it, and when editable, drag from a supplier or machine to another node to lay a
 * belt. Each node also has a focusable button over it, for the keyboard and screen readers.
 */
@Component({
  selector: 'app-factory-line-3d',
  imports: [NgIcon],
  viewProviders: [provideIcons({ lucideMaximize, lucideMinus, lucidePlus })],
  template: `
    <div class="l3d-zoom">
      <button type="button" (click)="zoomBy(1.25)" aria-label="Zoom in">
        <ng-icon name="lucidePlus" />
      </button>
      <button type="button" (click)="zoomBy(0.8)" aria-label="Zoom out">
        <ng-icon name="lucideMinus" />
      </button>
      <button type="button" (click)="refit()" aria-label="Fit to screen">
        <ng-icon name="lucideMaximize" />
      </button>
    </div>
    @if (noWebgl()) {
      <p class="l3d-nogl" role="alert">
        The factory floor needs WebGL, which this browser has off.
      </p>
    }
  `,
  styles: `
    app-factory-line-3d {
      display: block;
      position: relative;
      overflow: hidden;
      cursor: grab;
      &:active {
        cursor: grabbing;
      }
      canvas {
        display: block;
        outline: none;
      }
    }
    .l3d-labels {
      position: absolute;
      inset: 0;
      pointer-events: none;
    }
    .l3d-zoom {
      position: absolute;
      z-index: 2;
      right: 12px;
      bottom: 12px;
      display: flex;
      flex-direction: column;
      gap: 6px;
      button {
        display: grid;
        place-items: center;
        padding: 0;
        width: 36px;
        height: 36px;
        border-radius: 50%;
        border: 3px solid #1f2430;
        background: #fff;
        color: #1f2430;
        font:
          600 18px/1 'Fredoka',
          ui-rounded,
          system-ui,
          sans-serif;
        box-shadow: 0 3px 0 #1f2430;
        cursor: pointer;
        &:hover,
        &:focus-visible {
          background: #ffd84a;
        }
      }
    }
    .l3d-nogl {
      position: absolute;
      inset: 40% 16px auto;
      z-index: 2;
      text-align: center;
      color: #1f2430;
    }
    .l3d-tip {
      position: absolute;
      z-index: 3;
      pointer-events: none;
      padding: 3px 10px;
      background: rgba(31, 36, 48, 0.9);
      color: #fff;
      border-radius: 8px;
      font:
        500 13px 'Fredoka',
        ui-rounded,
        system-ui,
        sans-serif;
      white-space: nowrap;
      transform: translate(14px, 14px);
    }
    /* A node's button: an invisible disc over it that takes the keyboard and belt drags. */
    .node {
      .disc {
        width: 44px;
        height: 44px;
        border-radius: 50%;
        pointer-events: auto;
        cursor: pointer;
      }
      &.source .disc {
        cursor: crosshair;
      }
      &:focus-visible {
        outline: none;
        .disc {
          outline: 3px solid #ffd84a;
          outline-offset: 2px;
        }
      }
      .badge-main,
      .badge-sub {
        position: absolute;
        width: 1px;
        height: 1px;
        overflow: hidden;
        clip-path: inset(50%);
        white-space: nowrap;
      }
    }
    .l3d-label {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
      font-family: 'Fredoka', ui-rounded, 'SF Pro Rounded', system-ui, sans-serif;
      color: #1f2430;
      user-select: none;
      transform: scale(var(--k, 1));
      transform-origin: 50% 100%;
      svg {
        stroke-width: 2.5;
      }
      .pill {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 3px 12px 3px 7px;
        background: #fff;
        border: 3px solid #1f2430;
        border-radius: 999px;
        box-shadow: 0 4px 0 #1f2430;
        font-size: 15px;
        font-weight: 600;
        white-space: nowrap;
      }
      .icon {
        display: flex;
        svg {
          width: 20px;
          height: 20px;
        }
      }
      .sub {
        display: flex;
        align-items: center;
        gap: 4px;
        svg {
          width: 13px;
          height: 13px;
        }
        padding: 1px 8px;
        background: rgba(31, 36, 48, 0.82);
        color: #fff;
        border-radius: 8px;
        font-size: 12px;
        font-weight: 500;
        white-space: nowrap;
      }
      .sub:empty {
        display: none;
      }
      &.selected .pill {
        background: #ffd84a;
      }
    }
  `,
  encapsulation: ViewEncapsulation.None,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class FactoryLine3d {
  readonly level = input<FactoryLevel | null>(null);
  readonly design = input<Design | null>(null);
  readonly selection = input<FloorSelection>(null);
  /** During test playback: the hour being shown, which drives the lamps and belts. */
  readonly report = input<HourReport | null>(null);
  /** Allow dragging from a supplier or machine to another node to lay belts. */
  readonly editable = input(false);
  /** Pixels covered by panels on the left and right, kept clear when framing the floor. */
  readonly insets = input<[number, number]>([0, 0]);
  readonly nodeSelected = output<string | null>();
  readonly beltSelected = output<number>();
  /** The user dragged from one node to another. */
  readonly connect = output<{ from: string; to: string }>();
  protected readonly noWebgl = signal(false);

  private readonly host = inject<ElementRef<HTMLElement>>(ElementRef).nativeElement;
  private readonly destroyRef = inject(DestroyRef);
  private readonly motion =
    typeof matchMedia !== 'function' || !matchMedia('(prefers-reduced-motion: reduce)').matches;

  private renderer?: THREE.WebGLRenderer;
  private labels?: CSS2DRenderer;
  private camera = new THREE.PerspectiveCamera(38, 1, 0.5, 600);
  private controls?: OrbitControls;
  private readonly scene = new THREE.Scene();
  private readonly sun = new THREE.DirectionalLight('#fff1d8', 3.2);
  private readonly world = new THREE.Group();
  private kit = new Kit(this.motion);
  private beltTexture?: THREE.CanvasTexture;
  private ring?: THREE.Mesh;
  private floor?: FloorLayout;
  private hall?: Rect;
  private readonly nodes = new Map<string, NodePart>();
  /** Belts by key, and suppliers' feeds by `feed:` and the supplier's id. */
  private readonly belts = new Map<string, BeltPart>();
  /** The running belts' material, with the scrolling chevrons. */
  private runningBelt?: THREE.Material;
  /** Every belt's items, drawn as two instanced meshes: bodies and their bases. */
  private itemRuns: ItemRun[] = [];
  private itemMeshes: THREE.InstancedMesh[] = [];
  /** The belts' legs, as instanced meshes: those in focus and the faded ones. */
  private legMeshes: THREE.InstancedMesh[] = [];
  /** The selected node's or belt's name label, and its lines that change by the hour. */
  private label?: { obj: CSS2DObject; lines: HTMLElement[]; node?: string; belt?: string };
  /** Whether the user has moved the camera; until then it keeps the hall framed. */
  private moved = false;
  /** The hall stays between rebuilds while its size does: editing a design is quicker. */
  private hallKit = new Kit(this.motion);
  private hallGroup?: THREE.Group;
  private hallKey = '';
  /** What the pointer is over, focused like a selection while it stays there. */
  private hover: FloorSelection = null;
  /** A press on the floor: a click when it barely moves, a belt when dragged from a node. */
  private press?: { pick: Pick | null; x: number; y: number; laying: boolean; moved: boolean };
  /** The belt being laid: a glowing line to the pointer, and a ring on the node under it. */
  private dragLine?: THREE.Mesh;
  private dropRing?: THREE.Mesh;
  private tip?: HTMLElement;
  private readonly ray = new THREE.Raycaster();
  /** Whether the view must be drawn again. With motion everything animates, so every
   * frame is; without, only after something changes. */
  private dirty = true;
  private readonly timer = new THREE.Timer();

  constructor() {
    afterNextRender(() => {
      this.setup();
      this.build();
      this.fit();
    });
    effect(() => {
      this.level();
      this.design();
      if (this.renderer) untracked(() => this.build());
    });
    effect(() => {
      this.selection();
      if (this.renderer) untracked(() => this.applyFocus());
    });
    effect(() => {
      this.report();
      if (this.renderer) untracked(() => this.applyReport());
    });
    let lastLevel: string | undefined;
    effect(() => {
      const id = this.level()?.id;
      if (this.renderer && id !== lastLevel) {
        lastLevel = id;
        this.moved = false;
        this.fit();
      }
    });
    this.destroyRef.onDestroy(() => this.teardown());
  }

  private setup(): void {
    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true });
    } catch {
      this.noWebgl.set(true);
      return;
    }
    renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    // Shadows are drawn when the floor is built, not every frame: what moves (items, gears,
    // smoke) casts none, so they stay right.
    renderer.shadowMap.autoUpdate = false;
    // Without a graphics card (software WebGL), shadows and sharp pixels cost too much.
    const gl = renderer.getContext();
    const info = gl.getExtension('WEBGL_debug_renderer_info');
    const gpu = info ? String(gl.getParameter(info.UNMASKED_RENDERER_WEBGL)) : '';
    if (/swiftshader|llvmpipe|software/i.test(gpu)) {
      renderer.setPixelRatio(1);
      renderer.shadowMap.enabled = false;
    }
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    this.host.appendChild(renderer.domElement);
    this.renderer = renderer;

    const labels = new CSS2DRenderer();
    labels.domElement.className = 'l3d-labels';
    this.host.appendChild(labels.domElement);
    this.labels = labels;

    // A bright cartoon sky fading to haze at the horizon.
    this.scene.background = canvasTexture(4, 256, (c) => {
      const grad = c.createLinearGradient(0, 0, 0, 256);
      grad.addColorStop(0, '#3e9bf5');
      grad.addColorStop(0.6, '#8ccaff');
      grad.addColorStop(1, '#d9efff');
      c.fillStyle = grad;
      c.fillRect(0, 0, 4, 256);
    });
    this.scene.fog = new THREE.Fog('#cfe9ff', 120, 320);
    this.scene.add(new THREE.HemisphereLight('#e4f2ff', '#8a7a62', 1.15));
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(4096, 4096);
    this.sun.shadow.bias = -0.0004;
    this.sun.shadow.normalBias = 0.04;
    this.scene.add(this.sun, this.sun.target, this.world);

    // Chevrons on the rubber belts, scrolled forward each frame to make the belts run.
    this.beltTexture = canvasTexture(128, 64, (c) => {
      c.fillStyle = '#30353e';
      c.fillRect(0, 0, 128, 64);
      c.strokeStyle = '#4d5562';
      c.lineWidth = 12;
      c.lineCap = 'round';
      c.beginPath();
      c.moveTo(34, 8);
      c.lineTo(80, 32);
      c.lineTo(34, 56);
      c.stroke();
    });

    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(2.6, 0.16, 12, 48),
      new THREE.MeshStandardMaterial({ color: '#ffd84a', emissive: '#ffb800', roughness: 0.4 }),
    );
    ring.rotation.x = -Math.PI / 2;
    ring.visible = false;
    this.scene.add(ring);
    this.ring = ring;
    const drop = new THREE.Mesh(
      ring.geometry,
      new THREE.MeshStandardMaterial({ color: '#3ddc5f', emissive: '#1d9e3a', roughness: 0.4 }),
    );
    drop.rotation.x = -Math.PI / 2;
    drop.visible = false;
    this.scene.add(drop);
    this.dropRing = drop;
    const line = new THREE.Mesh(
      new THREE.CylinderGeometry(0.16, 0.16, 1, 12),
      new THREE.MeshStandardMaterial({ color: '#ffd84a', emissive: '#ffb800', roughness: 0.4 }),
    );
    line.visible = false;
    this.scene.add(line);
    this.dragLine = line;
    const tip = document.createElement('div');
    tip.className = 'l3d-tip';
    tip.hidden = true;
    this.host.appendChild(tip);
    this.tip = tip;

    const controls = new OrbitControls(this.camera, renderer.domElement);
    controls.enableDamping = true;
    controls.zoomToCursor = true;
    controls.maxPolarAngle = 1.32;
    controls.minDistance = 8;
    controls.maxDistance = 160;
    controls.screenSpacePanning = false;
    controls.addEventListener('start', () => (this.moved = true));
    controls.addEventListener('change', () => (this.dirty = true));
    this.controls = controls;

    this.listenForPointer(renderer.domElement);
    const ro = new ResizeObserver(() => this.resize());
    ro.observe(this.host);
    this.destroyRef.onDestroy(() => ro.disconnect());
    this.resize();

    renderer.setAnimationLoop(() => this.frame());
  }

  private resize(): void {
    const { clientWidth: w, clientHeight: h } = this.host;
    if (!w || !h || !this.renderer) return;
    this.renderer.setSize(w, h);
    this.labels?.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    this.dirty = true;
    if (!this.moved) this.fit();
  }

  private frame(): void {
    this.timer.update();
    const dt = Math.min(0.1, this.timer.getDelta());
    const t = this.timer.getElapsed();
    const moving = this.controls?.update() ?? false;
    if (!this.motion && !this.dirty && !moving) return;
    this.dirty = false;
    for (const tick of this.kit.tickers) tick(t, dt);
    for (const tick of this.hallKit.tickers) tick(t, dt);
    this.moveItems(this.motion ? t : 0);
    if (this.beltTexture && this.motion) this.beltTexture.offset.x = -((t * SPEED) / TILE);
    if (this.ring?.visible) this.ring.position.y = 0.3 + Math.sin(t * 3) * 0.08;
    // Labels shrink as the camera backs off, so they cover less of the line.
    if (this.labels && this.controls) {
      const k = THREE.MathUtils.clamp(
        42 / this.camera.position.distanceTo(this.controls.target),
        0.85,
        1.1,
      );
      this.labels.domElement.style.setProperty('--k', k.toFixed(3));
    }
    this.renderer?.render(this.scene, this.camera);
    this.labels?.render(this.scene, this.camera);
  }

  /** Clears the floor and builds it again from the level and design. */
  private build(): void {
    this.world.clear();
    this.nodes.clear();
    this.belts.clear();
    this.itemRuns = [];
    this.itemMeshes = [];
    this.legMeshes = [];
    this.label = undefined;
    this.kit.dispose();
    this.kit = new Kit(this.motion);
    const level = this.level();
    if (!level || !this.ring || !this.renderer) return;
    const floor = layoutFloor(level, this.design(), null, FLOOR_3D);
    this.floor = floor;
    const kit = this.kit;
    this.runningBelt = kit.own(
      new THREE.MeshStandardMaterial({ map: this.beltTexture, roughness: 0.85 }),
    );

    // The hall: room on the left for the trucks and on the right for the market.
    const xs = floor.nodes.map((n) => n.x * S);
    const zs = floor.nodes.map((n) => n.y * S);
    const hall: Rect = {
      x0: -10,
      x1: Math.max(...xs) + 10,
      z0: Math.min(...zs) - 6,
      z1: Math.max(...zs) + 5,
    };
    this.hall = hall;
    const bays = floor.nodes.filter((n) => n.kind === 'supplier').map((n) => n.y * S);
    const key = JSON.stringify([hall, bays]);
    if (!this.hallGroup || key !== this.hallKey) {
      this.hallKit.dispose();
      this.hallKit = new Kit(this.motion);
      this.hallGroup = hallModel(this.hallKit, hall, bays);
      this.hallKey = key;
      this.aimSun(hall);
    }
    this.world.add(this.hallGroup);

    for (const n of floor.nodes) if (n.kind === 'supplier') this.addFeed(n, level);
    const lines = floor.belts.map((b) => beltLine(b.points));
    bridge(
      lines,
      floor.belts.map((b) => b.points.map(([x, y]) => new THREE.Vector2(x * S, y * S))),
    );
    floor.belts.forEach((b, i) => this.addBelt(b, simplify(lines[i])));
    for (const n of floor.nodes) this.addNode(n, level);
    this.addItemMeshes();
    this.applyFocus();
    this.renderer.shadowMap.needsUpdate = true;
  }

  /**
   * The selected node's whole chain: everything upstream that it is made from, back to the
   * trucks, and everything downstream that it goes into, on to the market. For a selected
   * belt, the chain through that belt.
   */
  private focusOf(floor: FloorLayout, sel: FloorSelection): Focus | null {
    if (!sel) return null;
    const focus: Focus = { nodes: new Set(), belts: new Set() };
    // Follows belts one way only, so the chain never spreads into a sibling's inputs.
    const walk = (from: string[], dir: 'up' | 'down') => {
      const todo = [...from];
      while (todo.length) {
        const id = todo.pop()!;
        for (const b of floor.belts) {
          const [near, far] = dir === 'up' ? [b.to, b.from] : [b.from, b.to];
          if (near !== id || focus.belts.has(b.key)) continue;
          focus.belts.add(b.key);
          if (!focus.nodes.has(far)) todo.push(far);
          focus.nodes.add(far);
        }
      }
    };
    if (sel.kind === 'node') {
      const id = floor.groupOf.get(sel.id) ?? sel.id;
      focus.nodes.add(id);
      walk([id], 'up');
      walk([id], 'down');
    } else {
      const b = floor.belts.find((x) => x.indices.includes(sel.index));
      if (!b) return focus;
      focus.belts.add(b.key);
      focus.nodes.add(b.from).add(b.to);
      walk([b.from], 'up');
      walk([b.to], 'down');
    }
    return focus;
  }

  /** Shows the selection: fades what it does not touch, labels it and rings it. Nothing is
   * rebuilt, so clicking stays quick. */
  private applyFocus(): void {
    this.dirty = true;
    const floor = this.floor;
    if (!floor || !this.ring) return;
    const kit = this.kit;
    const sel = this.selection();
    const focus = this.focusOf(floor, this.hover ?? sel);
    for (const part of this.nodes.values()) {
      part.faded = !!focus && !focus.nodes.has(part.node.id);
      if (part.machine) part.machine.faded = part.faded;
      if (part.gauge) part.gauge.faded = part.faded;
      kit.fadeAll(part.group, part.faded);
    }
    for (const [key, part] of this.belts) {
      const owner = key.startsWith('feed:') ? key.slice(5) : null;
      part.conveyor.faded = !!focus && !(owner ? focus.nodes.has(owner) : focus.belts.has(key));
      part.conveyor.selected =
        !!part.belt && sel?.kind === 'belt' && part.belt.indices.includes(sel.index);
      kit.fadeAll(part.group, part.conveyor.faded);
    }
    this.addLegs();

    // The label and ring of the selected node, or the label of the selected belt.
    this.label?.obj.removeFromParent();
    this.label = undefined;
    this.ring.visible = false;
    const node =
      sel?.kind === 'node' ? this.nodes.get(floor.groupOf.get(sel.id) ?? sel.id) : undefined;
    const belt =
      sel?.kind === 'belt'
        ? [...this.belts.values()].find((p) => p.belt?.indices.includes(sel.index))
        : undefined;
    if (node) {
      const { x, y, z } = node.labelAt;
      this.label = { ...this.addLabel(x, y, z, node.icon, node.node.title), node: node.node.id };
      this.ring.visible = true;
      const n = node.node;
      this.ring.position.set(n.x * S + (n.kind === 'market' ? 3 : 0), 0.3, n.y * S);
    } else if (belt?.belt) {
      const mid = pointAt(belt.line, belt.line.length / 2);
      const r = belt.belt.resource;
      const name = this.level()?.resources.find((x) => x.id === r)?.name ?? r;
      this.label = {
        ...this.addLabel(mid.p.x, BELT_Y + mid.lift + 1.4, mid.p.y, iconFor(r), name),
        belt: belt.belt.key,
      };
    }
    this.applyReport();
  }

  /** Shows the hour: storage gauges, lamps, which machines work and which belts run. */
  private applyReport(): void {
    this.dirty = true;
    const level = this.level();
    if (!level || !this.floor) return;
    const report = this.report();
    // Only the hour's state is needed here; the routes were laid when the floor was built.
    const floor = layoutFloor(level, this.design(), report, {
      ...FLOOR_3D,
      route: false,
    });
    for (const n of floor.nodes) {
      const part = this.nodes.get(n.id);
      if (!part) continue;
      part.gauge?.fill(n.storage?.held ?? 0, n.storage?.cap ?? 0);
      part.machine?.set(
        LAMP[n.status ?? ''] ?? (n.active ? LAMP['busy'] : LAMP['starved']),
        report ? n.status === 'busy' : n.active,
      );
      part.badge.textContent = n.badge;
      part.sub.textContent = n.sub;
      if (this.label?.node === n.id) {
        setLine(this.label.lines[0], n.badgeIcon, n.badge);
        setLine(this.label.lines[1], n.subIcon, n.sub);
      }
      const feed = this.belts.get(`feed:${n.id}`);
      if (feed) this.run(feed, n.active);
    }
    for (const b of floor.belts) {
      const part = this.belts.get(b.key);
      if (part) this.run(part, b.active);
      if (this.label?.belt === b.key) {
        this.label.lines[0].textContent = `weight ${b.weight}`;
        this.label.lines[1].textContent = b.moved === null ? '' : `${b.moved} moved this hour`;
      }
    }
  }

  /** Starts or stops a belt; items only ride the belts in focus. */
  private run(part: BeltPart, running: boolean): void {
    part.conveyor.set(running);
    part.items.show = running && !part.conveyor.faded;
  }

  private addNode(n: FloorNode, level: FactoryLevel): void {
    const kit = this.kit;
    const [x, z] = [n.x * S, n.y * S];
    const g = new THREE.Group();
    const el = document.createElement('div');
    el.className = `node ${n.kind}`;
    el.setAttribute('role', 'button');
    el.tabIndex = 0;
    el.setAttribute('aria-label', n.title);
    const disc = document.createElement('div');
    disc.className = 'disc';
    const badge = document.createElement('span');
    badge.className = 'badge-main';
    const sub = document.createElement('span');
    sub.className = 'badge-sub';
    el.append(disc, badge, sub);
    const part: NodePart = {
      node: n,
      group: g,
      labelAt: new THREE.Vector3(),
      icon: n.kind === 'market' ? MARKET_ICON : n.icon,
      faded: false,
      el,
      badge,
      sub,
    };
    let signAt: [number, number, number];
    // What the sign shows: a supplier's resource, a machine's (first) product, or a coin.
    let shows = 'market';
    if (n.kind === 'supplier') {
      const resource = level.suppliers.find((s) => s.id === n.id)?.resource ?? '';
      g.add(supplierModel(kit, itemColor(resource)));
      if (n.storage) part.gauge = gaugeModel(kit);
      signAt = [0, 4.1, 0];
      shows = resource;
    } else if (n.kind === 'machine') {
      const placed = this.design()?.machines.find((m) => m.slot === n.id);
      const type = level.machine_types.find((t) => t.id === placed?.type);
      const recipe = type?.recipes.find((r) => r.id === placed?.recipe);
      shows = Object.keys(recipe?.outputs ?? {})[0] ?? '';
      const i = Math.max(0, level.machine_types.indexOf(type!));
      part.machine = machineModel(kit, MACHINE_COLORS[i % MACHINE_COLORS.length]);
      part.machine.group.scale.setScalar(1.2);
      g.add(part.machine.group);
      part.gauge = gaugeModel(kit);
      signAt = [0, 6.8, 0];
    } else {
      const targets = (this.floor?.belts ?? []).filter((b) => b.to === 'market');
      const tz = targets.map((b) => b.target[1] * S - z);
      const z0 = Math.min(-2.5, ...tz.map((v) => v - 1.6));
      const z1 = Math.max(2.5, ...tz.map((v) => v + 1.6));
      g.add(at(marketModel(kit, z1 - z0), 0, 0, (z0 + z1) / 2));
      signAt = [3, 6.4, (z0 + z1) / 2];
    }
    // Storage beside the node, at its front right corner where the camera sees it.
    if (part.gauge) {
      part.gauge.group.scale.setScalar(1.35);
      g.add(at(part.gauge.group, n.kind === 'machine' ? 2.75 : 2.3, 0, 1.2));
    }
    const sign = itemSign(kit, shows, n.count ?? 1);
    sign.position.set(...signAt);
    g.add(sign);
    if (this.motion) {
      const phase = x * 0.7 + z;
      kit.tickers.push((t) => (sign.position.y = signAt[1] + Math.sin(t * 2 + phase) * 0.12));
    }
    g.position.set(x, 0, z);
    g.userData['pick'] = { kind: 'node', id: n.id } satisfies Pick;
    part.labelAt.set(x + signAt[0], signAt[1] + 4, z + signAt[2]);
    this.world.add(g);
    this.nodes.set(n.id, part);

    // Its button, over the middle of its model.
    el.classList.toggle('source', this.editable() && n.kind !== 'market');
    const pick: Pick = { kind: 'node', id: n.id };
    disc.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      this.startPress(e, pick);
    });
    disc.addEventListener('pointerenter', () => this.setHover(pick, n.title));
    disc.addEventListener('pointerleave', () => this.setHover(null));
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        this.nodeSelected.emit(n.id);
      }
    });
    const anchor = new CSS2DObject(el);
    anchor.position.set(x + signAt[0], 1.8, z + signAt[2]);
    this.world.add(anchor);
  }

  /** A name label with two lines that change by the hour. */
  private addLabel(
    x: number,
    y: number,
    z: number,
    icon: string,
    title: string,
  ): { obj: CSS2DObject; lines: HTMLElement[] } {
    const outer = document.createElement('div');
    const el = document.createElement('div');
    el.className = 'l3d-label selected';
    outer.append(el);
    const pill = document.createElement('div');
    pill.className = 'pill';
    const i = document.createElement('span');
    i.className = 'icon';
    i.innerHTML = icon;
    const t = document.createElement('span');
    t.textContent = title;
    pill.append(i, t);
    el.append(pill);
    const lines = [0, 1].map(() => {
      const line = document.createElement('div');
      line.className = 'sub';
      el.append(line);
      return line;
    });
    const obj = new CSS2DObject(outer);
    obj.center.set(0.5, 1);
    obj.position.set(x, y, z);
    this.world.add(obj);
    return { obj, lines };
  }

  /** A conveyor along a belt's route, with its items riding on it while it runs. */
  private addBelt(b: FloorBelt, line: Line): void {
    const conveyor = this.conveyor(line);
    conveyor.group.userData['pick'] = { kind: 'belt', index: b.index } satisfies Pick;
    this.world.add(conveyor.group);
    this.belts.set(b.key, {
      belt: b,
      group: conveyor.group,
      line,
      legs: this.legsAlong(line),
      conveyor,
      items: this.items(line, b.resource),
    });
  }

  /** A supplier's feed: a truck at the left edge unloading onto a belt to the supplier. */
  private addFeed(n: FloorNode, level: FactoryLevel): void {
    const z = n.y * S;
    const resource = level.suppliers.find((s) => s.id === n.id)?.resource ?? '';
    const line = simplify(
      beltLine([
        [-0.4 / S, n.y],
        [n.x - R / 2, n.y],
      ]),
    );
    const conveyor = this.conveyor(line);
    const truck = truckModel(this.kit, itemColor(resource));
    truck.position.set(-2.6, 0, z);
    conveyor.group.add(truck);
    this.world.add(conveyor.group);
    this.belts.set(`feed:${n.id}`, {
      group: conveyor.group,
      line,
      legs: this.legsAlong(line),
      conveyor,
      items: this.items(line, resource),
    });
  }

  /** A conveyor: a frame, rails and a belt that runs or stands still. */
  private conveyor(line: Line): Conveyor {
    const kit = this.kit;
    const g = new THREE.Group();
    const hw = BELT_W / 2;
    const frame = kit.mesh(
      kit.own(sweep(line, rect(-hw - 0.06, hw + 0.06, BELT_Y - 0.3, BELT_Y - 0.02))),
      kit.plastic('#59616f'),
    );
    const railL = kit.mesh(
      kit.own(sweep(line, rect(hw - 0.02, hw + 0.08, BELT_Y - 0.06, BELT_Y + 0.14))),
      kit.plastic('#a9aeb6'),
    );
    const railR = kit.mesh(
      kit.own(sweep(line, rect(-hw - 0.08, -hw + 0.02, BELT_Y - 0.06, BELT_Y + 0.14))),
      kit.plastic('#a9aeb6'),
    );
    const surface = kit.mesh(
      kit.own(
        sweep(
          line,
          [
            [hw, BELT_Y],
            [-hw, BELT_Y],
          ],
          false,
        ),
      ),
      kit.plastic('#3a3f48', { roughness: 0.9 }),
    );
    g.add(frame, railL, railR, surface);
    const conveyor: Conveyor = {
      group: g,
      selected: false,
      faded: false,
      set: (running) => {
        const rails = conveyor.selected
          ? kit.plastic('#ffd84a', { emissive: '#b58a00' })
          : kit.plastic(running ? '#f5c518' : '#a9aeb6');
        kit.paint(railL, rails, conveyor.faded);
        kit.paint(railR, rails, conveyor.faded);
        kit.paint(
          surface,
          running && this.runningBelt
            ? this.runningBelt
            : kit.plastic('#3a3f48', { roughness: 0.9 }),
          conveyor.faded,
        );
      },
    };
    conveyor.set(false);
    return conveyor;
  }

  /** Legs every so often along a belt, and closer together under bridges. */
  private legsAlong(line: Line): THREE.Vector3[] {
    const hw = BELT_W / 2;
    const legs: THREE.Vector3[] = [];
    for (let k = 0, d = 0.9; d < line.length - 0.6; k++, d += 1.2) {
      const { p, angle, lift } = pointAt(line, d);
      if (k % 2 && lift < 0.3) continue;
      for (const s of [-1, 1])
        legs.push(
          new THREE.Vector3(
            p.x - Math.sin(angle) * s * hw,
            BELT_Y - 0.3 + lift,
            p.y + Math.cos(angle) * s * hw,
          ),
        );
    }
    return legs;
  }

  /** All the legs as two instanced meshes, those in focus and the faded ones: a big
   * factory has hundreds. */
  private addLegs(): void {
    const kit = this.kit;
    for (const m of this.legMeshes) {
      m.removeFromParent();
      m.dispose();
    }
    this.legMeshes = [];
    const parts = [...this.belts.values()];
    for (const faded of [false, true]) {
      const at = parts.filter((p) => p.conveyor.faded === faded).flatMap((p) => p.legs);
      if (!at.length) continue;
      const geo = kit.geo('leg', () => new THREE.BoxGeometry(0.12, 1, 0.12));
      const legs = new THREE.InstancedMesh(geo, kit.look(kit.plastic('#3d4350'), faded), at.length);
      const m = new THREE.Matrix4();
      const one = new THREE.Quaternion();
      at.forEach(({ x, y: h, z }, i) =>
        legs.setMatrixAt(
          i,
          m.compose(new THREE.Vector3(x, h / 2, z), one, new THREE.Vector3(1, h, 1)),
        ),
      );
      legs.castShadow = !faded;
      legs.receiveShadow = true;
      this.world.add(legs);
      this.legMeshes.push(legs);
    }
  }

  /** Reserves the items riding a belt in the shared item meshes. */
  private items(line: Line, resource: string): ItemRun {
    const n = Math.max(1, Math.floor(line.length / ITEM_GAP));
    const first = this.itemRuns.reduce((t, r) => t + r.n, 0);
    const run: ItemRun = { line, first, n, color: itemColor(resource), show: false };
    this.itemRuns.push(run);
    return run;
  }

  /** The items of every belt as two instanced meshes: their bodies and their bases. */
  private addItemMeshes(): void {
    const kit = this.kit;
    const total = this.itemRuns.reduce((t, r) => t + r.n, 0);
    if (!total) return;
    const body = new THREE.InstancedMesh(
      kit.geo('item', () => new THREE.IcosahedronGeometry(0.23, 1)),
      kit.plastic('#ffffff', { roughness: 0.45, flatShading: true }),
      total,
    );
    const base = new THREE.InstancedMesh(
      kit.geo('item-base', () => new RoundedBoxGeometry(0.4, 0.12, 0.4, 2, 0.05)),
      kit.plastic('#ffffff'),
      total,
    );
    for (const run of this.itemRuns)
      for (let i = 0; i < run.n; i++) {
        body.setColorAt(run.first + i, new THREE.Color(run.color));
        base.setColorAt(run.first + i, shade(run.color, -0.15));
      }
    for (const m of [body, base]) {
      m.castShadow = false;
      m.receiveShadow = true;
      m.frustumCulled = false;
      this.world.add(kit.own(m));
    }
    this.itemMeshes = [body, base];
    this.moveItems(0);
  }

  /** Moves the items along their belts; items of belts that stand still are hidden. */
  private moveItems(t: number): void {
    const [body, base] = this.itemMeshes;
    if (!body || !base) return;
    const m = new THREE.Matrix4();
    const q = new THREE.Quaternion();
    const up = new THREE.Vector3(0, 1, 0);
    const pos = new THREE.Vector3();
    const one = new THREE.Vector3(1, 1, 1);
    const none = new THREE.Vector3(0, 0, 0);
    for (const run of this.itemRuns)
      for (let i = 0; i < run.n; i++) {
        const d = (t * SPEED + (i / run.n) * run.line.length) % run.line.length;
        const { p, angle, lift } = pointAt(run.line, d);
        q.setFromAxisAngle(up, -angle);
        const scale = run.show ? one : none;
        body.setMatrixAt(
          run.first + i,
          m.compose(pos.set(p.x, BELT_Y + 0.3 + lift, p.y), q, scale),
        );
        base.setMatrixAt(
          run.first + i,
          m.compose(pos.set(p.x, BELT_Y + 0.13 + lift, p.y), q, scale),
        );
      }
    body.instanceMatrix.needsUpdate = true;
    base.instanceMatrix.needsUpdate = true;
  }

  /** Points the sun across the hall and fits its shadows to it. */
  private aimSun(r: Rect): void {
    const cx = (r.x0 + r.x1) / 2;
    const cz = (r.z0 + r.z1) / 2;
    const size = Math.max(r.x1 - r.x0, r.z1 - r.z0) / 2 + 6;
    this.sun.position.set(cx - 25, 45, cz + 30);
    this.sun.target.position.set(cx, 0, cz);
    const cam = this.sun.shadow.camera;
    cam.left = cam.bottom = -size;
    cam.right = cam.top = size;
    cam.near = 1;
    cam.far = 160;
    cam.updateProjectionMatrix();
  }

  /** Frames the whole hall, wall included, looking down from the front with sky above, in
   * the part of the screen the side panels leave clear. */
  private fit(): void {
    const r = this.hall;
    if (!r || !this.controls) return;
    const cx = (r.x0 + r.x1) / 2;
    const cz = (r.z0 + r.z1) / 2;
    const tilt = THREE.MathUtils.degToRad(48);
    const dir = new THREE.Vector3(0, Math.sin(tilt), Math.cos(tilt));
    const target = new THREE.Vector3(cx, 0, cz);
    // The clear part of the screen, from -1 (left edge) to 1 (right edge).
    const width = this.host.clientWidth || 1;
    const [left, right] = width >= 900 ? this.insets() : [0, 0];
    const x0 = -1 + (2 * left) / width;
    const x1 = 1 - (2 * right) / width;
    // The floor's corners, and the top of the back wall.
    const corners = [r.x0, r.x1].flatMap((x) => [
      new THREE.Vector3(x, 0, r.z1),
      new THREE.Vector3(x, 0, r.z0),
      new THREE.Vector3(x, 9, r.z0),
    ]);
    const place = (dist: number) => {
      this.camera.position.copy(target).addScaledVector(dir, dist);
      this.camera.lookAt(target);
      this.camera.updateMatrixWorld();
      return corners.map((c) => c.clone().project(this.camera));
    };
    // Back off until every corner of the hall is in the clear part, sliding the view each
    // time so the hall sits in its middle. Corners behind the camera project to nonsense, so
    // it only slides once they are all in front.
    const home = target.clone();
    for (let dist = 10; dist < 400; dist *= 1.04) {
      target.copy(home);
      if (place(dist).some((p) => p.z >= 1)) continue;
      for (let i = 0; i < 6; i++) {
        const ps = place(dist);
        const [xs, ys] = [ps.map((p) => p.x), ps.map((p) => p.y)];
        const midY = (Math.max(...ys) + Math.min(...ys)) / 2;
        const midX = (Math.max(...xs) + Math.min(...xs)) / 2 - (x0 + x1) / 2;
        target.z -= THREE.MathUtils.clamp(midY * dist * 0.35, -3, 3);
        target.x += THREE.MathUtils.clamp(midX * dist * 0.35, -3, 3);
      }
      const ps = place(dist);
      if (ps.every((p) => p.x > x0 + 0.03 && p.x < x1 - 0.03 && Math.abs(p.y) < 0.93 && p.z < 1))
        break;
    }
    // The controls would pull a wide floor's camera back in to their usual limit.
    const dist = this.camera.position.distanceTo(target);
    this.controls.maxDistance = Math.max(160, dist * 1.5);
    // Haze only past the hall, however far back the camera is.
    const fog = this.scene.fog as THREE.Fog;
    fog.near = Math.max(120, dist + 60);
    fog.far = fog.near + 200;
    this.controls.target.copy(target);
    this.controls.update();
    this.dirty = true;
  }

  /** Zooms toward the middle of the view by `k`. */
  protected zoomBy(k: number): void {
    if (!this.controls) return;
    const t = this.controls.target;
    this.camera.position
      .sub(t)
      .multiplyScalar(1 / k)
      .add(t);
    this.moved = true;
    this.dirty = true;
  }

  /** Frames the whole hall again. */
  protected refit(): void {
    this.moved = false;
    this.fit();
  }

  /** The node or belt under a point on the screen. */
  private pickAt(clientX: number, clientY: number): Pick | null {
    const el = this.renderer?.domElement;
    if (!el) return null;
    const box = el.getBoundingClientRect();
    this.ray.setFromCamera(
      new THREE.Vector2(
        ((clientX - box.left) / box.width) * 2 - 1,
        -((clientY - box.top) / box.height) * 2 + 1,
      ),
      this.camera,
    );
    for (const hit of this.ray.intersectObjects(this.world.children, true))
      for (let o: THREE.Object3D | null = hit.object; o; o = o.parent) {
        const pick = o.userData['pick'] as Pick | undefined;
        if (pick) return pick;
      }
    return null;
  }

  /**
   * Presses on the floor: a press that barely moves is a click and selects what is under
   * it; when editable, dragging from a supplier or machine lays a belt to the node it is
   * dropped on; any other drag orbits the camera.
   */
  private listenForPointer(el: HTMLElement): void {
    // Capture runs before the orbit controls, so a belt drag can stop them orbiting.
    el.addEventListener(
      'pointerdown',
      (e) => this.startPress(e, this.pickAt(e.clientX, e.clientY)),
      {
        capture: true,
      },
    );
    let queued = false;
    el.addEventListener('pointermove', (e) => {
      if (this.press || e.buttons || queued) return;
      queued = true;
      requestAnimationFrame(() => {
        queued = false;
        const pick = this.pickAt(e.clientX, e.clientY);
        this.setHover(pick, pick && this.describe(pick));
        this.moveTip(e);
        el.style.cursor = pick ? 'pointer' : '';
      });
    });
    el.addEventListener('pointerleave', () => this.setHover(null));
    const move = (e: PointerEvent) => this.dragTo(e);
    const up = (e: PointerEvent) => this.endPress(e);
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    this.destroyRef.onDestroy(() => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    });
  }

  private startPress(e: PointerEvent, pick: Pick | null): void {
    if (e.button !== 0) return;
    const node = pick?.kind === 'node' ? this.nodes.get(pick.id)?.node : undefined;
    const laying = this.editable() && !!node && node.kind !== 'market';
    this.press = { pick, x: e.clientX, y: e.clientY, laying, moved: false };
    if (laying && this.controls) this.controls.enabled = false;
  }

  /** While laying a belt: a line from its node to the pointer, snapping to a node under it. */
  private dragTo(e: PointerEvent): void {
    const press = this.press;
    if (!press) return;
    if (Math.hypot(e.clientX - press.x, e.clientY - press.y) > 6) press.moved = true;
    if (!press.laying || !press.moved || press.pick?.kind !== 'node') return;
    const from = this.nodes.get(press.pick.id);
    const line = this.dragLine;
    if (!from || !line || !this.dropRing) return;
    const target = this.dropTarget(e, press.pick.id);
    const a = new THREE.Vector3(from.node.x * S, 1.8, from.node.y * S);
    let b: THREE.Vector3;
    if (target) {
      b = new THREE.Vector3(
        target.node.x * S + (target.node.kind === 'market' ? 3 : 0),
        1.8,
        target.node.y * S,
      );
      this.dropRing.position.set(b.x, 0.3, b.z);
    } else {
      b = new THREE.Vector3();
      if (!this.ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 1, 0), -1.8), b))
        return;
    }
    this.dropRing.visible = !!target;
    const d = b.clone().sub(a);
    line.position.copy(a).addScaledVector(d, 0.5);
    line.scale.set(1, Math.max(0.01, d.length()), 1);
    line.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
    line.visible = true;
    this.tip!.hidden = true;
    this.dirty = true;
  }

  /** The node a belt being laid from `from` would go to: the one under the pointer. */
  private dropTarget(e: PointerEvent, from: string): NodePart | undefined {
    const pick = this.pickAt(e.clientX, e.clientY);
    return pick?.kind === 'node' && pick.id !== from ? this.nodes.get(pick.id) : undefined;
  }

  private endPress(e: PointerEvent): void {
    const press = this.press;
    if (!press) return;
    this.press = undefined;
    if (this.controls) this.controls.enabled = true;
    if (this.dragLine) this.dragLine.visible = false;
    if (this.dropRing) this.dropRing.visible = false;
    this.dirty = true;
    if (press.laying && press.moved && press.pick?.kind === 'node') {
      const target = this.dropTarget(e, press.pick.id);
      if (target) this.connect.emit({ from: press.pick.id, to: target.node.id });
      return;
    }
    if (press.moved) return;
    if (press.pick?.kind === 'node') this.nodeSelected.emit(press.pick.id);
    else if (press.pick?.kind === 'belt') this.beltSelected.emit(press.pick.index);
    else this.nodeSelected.emit(null);
  }

  /** Focuses what the pointer is over, like a selection, and names it in a tooltip. */
  private setHover(pick: Pick | null, text?: string | null): void {
    const next: FloorSelection = pick;
    const same =
      next?.kind === this.hover?.kind &&
      (next?.kind === 'node'
        ? next.id === (this.hover as { id: string }).id
        : next?.kind === 'belt'
          ? next.index === (this.hover as { index: number }).index
          : true);
    if (this.tip) {
      this.tip.hidden = !text;
      this.tip.textContent = text ?? '';
    }
    if (same) return;
    this.hover = next;
    this.applyFocus();
  }

  private moveTip(e: PointerEvent): void {
    if (!this.tip) return;
    const box = this.host.getBoundingClientRect();
    this.tip.style.left = `${e.clientX - box.left}px`;
    this.tip.style.top = `${e.clientY - box.top}px`;
  }

  /** What a node or belt is, for its tooltip: a belt says where it runs and what it carries. */
  private describe(pick: Pick): string {
    if (pick.kind === 'node') return this.nodes.get(pick.id)?.node.title ?? '';
    const part = [...this.belts.values()].find((p) => p.belt?.indices.includes(pick.index));
    const b = part?.belt;
    if (!b) return '';
    const title = (id: string) => this.nodes.get(id)?.node.title ?? id;
    const name = this.level()?.resources.find((r) => r.id === b.resource)?.name ?? b.resource;
    return `${name}: ${title(b.from)} → ${title(b.to)}`;
  }

  private teardown(): void {
    this.renderer?.setAnimationLoop(null);
    this.controls?.dispose();
    this.world.clear();
    this.kit.dispose();
    this.hallKit.dispose();
    this.beltTexture?.dispose();
    (this.scene.background as THREE.Texture | null)?.dispose();
    this.renderer?.dispose();
    this.renderer?.domElement.remove();
    this.labels?.domElement.remove();
  }
}
