import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

/** An animation step run every frame, given the time and the time since the last frame. */
export type Ticker = (t: number, dt: number) => void;

/** A floor rectangle in world units: x across, z from the back wall to the front. */
export interface Rect {
  x0: number;
  x1: number;
  z0: number;
  z1: number;
}

/** Lighter (positive) or darker (negative) shade of a color. */
export function shade(color: THREE.ColorRepresentation, by: number): THREE.Color {
  return new THREE.Color(color).offsetHSL(0, 0, by);
}

/** A canvas-drawn texture that repeats. */
export function canvasTexture(
  w: number,
  h: number,
  draw: (g: CanvasRenderingContext2D) => void,
): THREE.CanvasTexture {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  draw(c.getContext('2d')!);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.anisotropy = 8;
  return t;
}

/**
 * Shared toy-plastic materials and rounded-box geometries, the animations that run each
 * frame, and everything to dispose when the scene is rebuilt.
 */
export class Kit {
  readonly tickers: Ticker[] = [];
  private readonly mats = new Map<string, THREE.MeshStandardMaterial>();
  private readonly geos = new Map<string, THREE.BufferGeometry>();
  private readonly owned: { dispose(): void }[] = [];
  private readonly fadedMats = new Map<THREE.Material, THREE.Material>();

  constructor(readonly motion: boolean) {}

  /** Matte plastic in `color`, shared by everything of that color. */
  plastic(
    color: THREE.ColorRepresentation,
    opts: THREE.MeshStandardMaterialParameters = {},
  ): THREE.MeshStandardMaterial {
    const key = `${new THREE.Color(color).getHexString()}|${JSON.stringify(opts)}`;
    let m = this.mats.get(key);
    if (!m) {
      m = new THREE.MeshStandardMaterial({ color, roughness: 0.55, metalness: 0, ...opts });
      this.mats.set(key, m);
    }
    return m;
  }

  /** A material of its own, for things that animate their color or opacity. */
  own<T extends { dispose(): void }>(thing: T): T {
    this.owned.push(thing);
    return thing;
  }

  geo(key: string, make: () => THREE.BufferGeometry): THREE.BufferGeometry {
    let g = this.geos.get(key);
    if (!g) {
      g = make();
      this.geos.set(key, g);
    }
    return g;
  }

  mesh(geo: THREE.BufferGeometry, mat: THREE.Material, shadows = true): THREE.Mesh {
    const m = new THREE.Mesh(geo, mat);
    m.castShadow = shadows;
    m.receiveShadow = true;
    return m;
  }

  /** A box with rounded edges, the basic toy brick. */
  box(
    w: number,
    h: number,
    d: number,
    color: THREE.ColorRepresentation,
    r = 0.12,
    opts: THREE.MeshStandardMaterialParameters = {},
  ): THREE.Mesh {
    const radius = Math.min(r, w / 2 - 0.01, h / 2 - 0.01, d / 2 - 0.01);
    const geo = this.geo(`box|${w}|${h}|${d}|${radius}`, () => {
      return new RoundedBoxGeometry(w, h, d, 2, radius);
    });
    return this.mesh(geo, this.plastic(color, opts));
  }

  cylinder(
    rTop: number,
    rBottom: number,
    h: number,
    color: THREE.ColorRepresentation,
    segments = 20,
  ): THREE.Mesh {
    const geo = this.geo(`cyl|${rTop}|${rBottom}|${h}|${segments}`, () => {
      return new THREE.CylinderGeometry(rTop, rBottom, h, segments);
    });
    return this.mesh(geo, this.plastic(color));
  }

  /** A see-through copy of `mat`, for things outside the selection. */
  faded(mat: THREE.Material): THREE.Material {
    let m = this.fadedMats.get(mat);
    if (!m) {
      m = mat.clone();
      m.transparent = true;
      m.opacity = mat.opacity * 0.16;
      m.depthWrite = false;
      this.fadedMats.set(mat, m);
    }
    return m;
  }

  /** `mat`, or its see-through copy when `faded`. */
  look(mat: THREE.Material, faded: boolean): THREE.Material {
    return faded ? this.faded(mat) : mat;
  }

  /** Gives `o` the material `mat`, see-through when `faded`; `fadeAll` keeps it. */
  paint(o: THREE.Mesh | THREE.Sprite, mat: THREE.Material, faded: boolean): void {
    o.userData['base'] = mat;
    o.material = this.look(mat, faded);
  }

  /** Shows everything in `o` normally, or see-through when it is outside the selection. */
  fadeAll(o: THREE.Object3D, faded: boolean): void {
    o.traverse((x) => {
      if (!(x instanceof THREE.Mesh || x instanceof THREE.Sprite)) return;
      x.userData['base'] ??= x.material;
      x.material = this.look(x.userData['base'] as THREE.Material, faded);
    });
  }

  dispose(): void {
    for (const m of this.fadedMats.values()) m.dispose();
    this.fadedMats.clear();
    for (const m of this.mats.values()) m.dispose();
    for (const g of this.geos.values()) g.dispose();
    for (const o of this.owned) o.dispose();
    this.mats.clear();
    this.geos.clear();
    this.owned.length = 0;
    this.tickers.length = 0;
  }
}

/**
 * Merges the fixed meshes under `root` into one mesh per material, so a model made of
 * dozens of bricks draws in a few calls. Parts marked `userData.live` (they move or change
 * color) are left as they are.
 */
export function bake<T extends THREE.Object3D>(kit: Kit, root: T): T {
  root.updateMatrixWorld(true);
  const inv = root.matrixWorld.clone().invert();
  const byMat = new Map<THREE.Material, THREE.BufferGeometry[]>();
  const done: THREE.Mesh[] = [];
  const walk = (o: THREE.Object3D) => {
    if (o !== root && o.userData['live']) return;
    if (o instanceof THREE.Mesh && !(o instanceof THREE.InstancedMesh)) {
      let g = (o.geometry as THREE.BufferGeometry).clone();
      if (g.index) g = g.toNonIndexed();
      for (const name of Object.keys(g.attributes))
        if (!['position', 'normal', 'uv'].includes(name)) g.deleteAttribute(name);
      g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(inv, o.matrixWorld));
      const mat = o.material as THREE.Material;
      byMat.set(mat, [...(byMat.get(mat) ?? []), g]);
      done.push(o);
    }
    o.children.forEach(walk);
  };
  walk(root);
  for (const m of done) m.removeFromParent();
  for (const [mat, geos] of byMat) {
    const merged = mergeGeometries(geos);
    geos.forEach((g) => g.dispose());
    if (merged) root.add(kit.mesh(kit.own(merged), mat));
  }
  return root;
}

const at = <T extends THREE.Object3D>(o: T, x: number, y: number, z: number): T => {
  o.position.set(x, y, z);
  return o;
};

/** A yellow gear that spins while `spinning()`. */
function gear(kit: Kit, spinning: () => boolean): THREE.Group {
  const g = new THREE.Group();
  const yellow = '#f5c518';
  const disc = kit.cylinder(0.5, 0.5, 0.2, yellow);
  disc.rotation.x = Math.PI / 2;
  g.add(disc);
  for (let i = 0; i < 8; i++) {
    const a = (i / 8) * Math.PI * 2;
    const tooth = kit.box(0.24, 0.24, 0.2, yellow, 0.04);
    tooth.position.set(Math.cos(a) * 0.6, Math.sin(a) * 0.6, 0);
    tooth.rotation.z = a;
    g.add(tooth);
  }
  const hub = kit.cylinder(0.16, 0.16, 0.3, '#3d4350', 12);
  hub.rotation.x = Math.PI / 2;
  g.add(hub);
  bake(kit, g);
  g.userData['live'] = true;
  if (kit.motion) kit.tickers.push((_, dt) => spinning() && (g.rotation.z -= dt * 1.8));
  return g;
}

/** A lamp whose material glows in its color, pulsing while `pulse()`. */
function lamp(
  kit: Kit,
  pulse: () => boolean,
): { mesh: THREE.Mesh; mat: THREE.MeshStandardMaterial } {
  const mat = kit.own(new THREE.MeshStandardMaterial({ emissiveIntensity: 1.2, roughness: 0.3 }));
  const geo = kit.geo('lamp', () => new THREE.SphereGeometry(0.26, 20, 14));
  const mesh = kit.mesh(geo, mat);
  mesh.userData['live'] = true;
  if (kit.motion)
    kit.tickers.push(
      (t) => (mat.emissiveIntensity = pulse() ? 0.8 + 0.6 * Math.sin(t * 4) ** 2 : 1),
    );
  return { mesh, mat };
}

/** Puffs of smoke rising from a point while a machine works. */
function smoke(kit: Kit, x: number, y: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.userData['live'] = true;
  const geo = kit.geo('puff', () => new THREE.IcosahedronGeometry(0.4, 1));
  const puffs = [0, 1, 2, 3].map(() => {
    const mat = kit.own(
      new THREE.MeshStandardMaterial({ color: '#f4f6f8', roughness: 1, transparent: true }),
    );
    const m = new THREE.Mesh(geo, mat);
    g.add(m);
    return { m, mat };
  });
  const step = (t: number) =>
    puffs.forEach(({ m, mat }, i) => {
      const p = (t * 0.45 + i / puffs.length) % 1;
      m.position.set(x + Math.sin(p * 3 + i) * 0.25, y + p * 2.4, z - p * 0.4);
      m.scale.setScalar(0.45 + p * 0.9);
      mat.opacity = 0.85 * (1 - p);
    });
  step(0);
  if (kit.motion) kit.tickers.push(step);
  return g;
}

/** A machine's look that changes hour by hour during playback. */
export interface MachineLive {
  group: THREE.Group;
  /** Lamp color, and whether it works this hour: the gear spins and the chimney puffs. */
  set(lampColor: string, working: boolean): void;
  /** Outside the selection: no smoke, and the lamp stays as it is. */
  faded: boolean;
}

/**
 * A machine: a chunky body in its type's color on a steel plinth, with a window, a hopper,
 * a chimney, a gear and a status lamp. While it works the gear spins and the chimney puffs.
 */
export function machineModel(kit: Kit, color: string): MachineLive {
  const g = new THREE.Group();
  let working = false;
  g.add(at(kit.box(3.7, 0.35, 3.5, '#4a5160', 0.1), 0, 0.175, 0));
  g.add(at(kit.box(3.2, 2.2, 3.0, color, 0.3), 0, 1.45, 0));
  g.add(at(kit.box(3.36, 0.32, 3.16, shade(color, -0.12), 0.14), 0, 2.62, 0));
  // Front: a dark window with a light frame, and a hazard band near the floor.
  g.add(at(kit.box(1.5, 0.95, 0.16, shade(color, 0.18), 0.08), -0.55, 1.6, 1.46));
  g.add(
    at(
      kit.box(1.25, 0.72, 0.14, '#22324a', 0.06, { roughness: 0.12, metalness: 0.2 }),
      -0.55,
      1.6,
      1.5,
    ),
  );
  g.add(at(kit.box(3.22, 0.22, 3.02, '#2b2f36', 0.06), 0, 0.5, 0));
  g.add(
    at(
      gear(kit, () => working),
      0.85,
      1.45,
      1.6,
    ),
  );
  // Top: a hopper the inputs drop into, and a chimney at the back.
  const hopper = kit.cylinder(0.85, 0.5, 0.8, shade(color, 0.12));
  g.add(at(hopper, -0.6, 3.18, -0.15));
  g.add(at(kit.cylinder(0.88, 0.88, 0.12, shade(color, -0.2)), -0.6, 3.6, -0.15));
  g.add(at(kit.cylinder(0.3, 0.36, 1.7, '#8d95a3', 16), 0.95, 3.6, -0.8));
  g.add(at(kit.cylinder(0.4, 0.4, 0.2, '#4a5160', 16), 0.95, 4.45, -0.8));
  g.add(at(kit.cylinder(0.08, 0.08, 0.5, '#4a5160', 8), 0.95, 3.05, 0.85));
  const light = lamp(kit, () => working);
  g.add(at(light.mesh, 0.95, 3.4, 0.85));
  const puffs = smoke(kit, 0.95, 4.7, -0.8);
  g.add(puffs);
  bake(kit, g);
  const live: MachineLive = {
    group: g,
    faded: false,
    set(lampColor, on) {
      working = on;
      light.mat.color.set(lampColor);
      light.mat.emissive.set(lampColor);
      puffs.visible = on && !live.faded;
    },
  };
  return live;
}

/** A storage gauge: a column of segments lit from the bottom as storage fills. */
export interface Gauge {
  group: THREE.Group;
  fill(held: number, cap: number): void;
  faded: boolean;
}

/** Segments in a storage gauge, like the wedges of the 2D storage ring. */
export const GAUGE_SEGMENTS = 6;

export function gaugeModel(kit: Kit): Gauge {
  const g = new THREE.Group();
  g.userData['live'] = true;
  g.add(at(kit.box(0.62, 0.22, 0.62, '#3d4350', 0.06), 0, 0.11, 0));
  g.add(at(kit.box(0.5, 2.5, 0.5, '#2b2f36', 0.1), 0, 1.45, 0));
  const off = kit.plastic('#4f5866');
  const lit = (c: string) => kit.plastic(c, { emissive: c, emissiveIntensity: 0.55 });
  const segments = Array.from({ length: GAUGE_SEGMENTS }, (_, i) => {
    const m = kit.box(0.56, 0.3, 0.56, '#4f5866', 0.05);
    m.position.y = 0.48 + i * 0.37;
    g.add(m);
    return m;
  });
  const gauge: Gauge = {
    group: g,
    faded: false,
    fill(held, cap) {
      const frac = cap > 0 ? Math.min(1, held / cap) : 0;
      const n = held > 0 ? Math.max(1, Math.round(frac * GAUGE_SEGMENTS)) : 0;
      const color = frac >= 0.85 ? '#ff4d4d' : frac >= 0.5 ? '#ffb020' : '#3ddc5f';
      segments.forEach((m, i) => kit.paint(m, i < n ? lit(color) : off, gauge.faded));
    },
  };
  gauge.fill(0, 0);
  return gauge;
}

/** A supplier: crates of its ingredient stacked on a wooden pallet. */
export function supplierModel(kit: Kit, color: string): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(3.4, 0.3, 3.4, '#b07b4a', 0.06), 0, 0.15, 0));
  for (const [x, z] of [
    [-0.8, -0.8],
    [0.8, -0.8],
    [-0.8, 0.8],
    [0.8, 0.8],
  ])
    g.add(crate(kit, color, x, 1.0, z));
  g.add(crate(kit, shade(color, 0.08), -0.35, 2.4, -0.3));
  g.add(crate(kit, color, 0.55, 2.4, 0.5));
  return bake(kit, g);
}

/** A crate: a box with a darker lid and slats. */
function crate(kit: Kit, color: THREE.ColorRepresentation, x: number, y: number, z: number) {
  const g = new THREE.Group();
  g.add(kit.box(1.4, 1.4, 1.4, color, 0.14));
  g.add(at(kit.box(1.48, 0.18, 1.48, shade(color, -0.15), 0.06), 0, 0.66, 0));
  g.add(at(kit.box(1.48, 0.14, 1.48, shade(color, -0.15), 0.05), 0, -0.2, 0));
  return at(g, x, y, z);
}

/** A delivery truck facing left, its open back at the right where its belt starts. */
export function truckModel(kit: Kit, color: string): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(4.2, 2.6, 2.6, '#f4f1ea', 0.22), 0, 2.0, 0));
  g.add(at(kit.box(4.24, 0.5, 2.64, color, 0.1), 0, 2.3, 0));
  g.add(at(kit.box(5.9, 0.35, 2.3, '#3d4350', 0.08), -0.7, 0.75, 0));
  g.add(at(kit.box(1.7, 1.9, 2.5, '#e2453c', 0.32), -2.9, 1.65, 0));
  g.add(
    at(
      kit.box(0.14, 0.75, 2.1, '#2e4a6b', 0.05, { roughness: 0.1, metalness: 0.3 }),
      -3.72,
      2.05,
      0,
    ),
  );
  g.add(at(kit.box(0.2, 0.32, 0.5, '#fff6c2', 0.06, { emissive: '#ffe680' }), -3.75, 1.1, 0.8));
  g.add(at(kit.box(0.2, 0.32, 0.5, '#fff6c2', 0.06, { emissive: '#ffe680' }), -3.75, 1.1, -0.8));
  for (const x of [-2.8, 1.2])
    for (const z of [-1.2, 1.2]) {
      const w = kit.cylinder(0.55, 0.55, 0.45, '#262a31', 20);
      w.rotation.x = Math.PI / 2;
      g.add(at(w, x, 0.55, z));
      const hub = kit.cylinder(0.25, 0.25, 0.5, '#b9bec6', 12);
      hub.rotation.x = Math.PI / 2;
      g.add(at(hub, x, 0.55, z));
    }
  return bake(kit, g);
}

/**
 * The market: a shop whose left wall takes the belts in under a striped awning, with a
 * pile of coins out front. `depth` is how far it runs from back to front.
 */
export function marketModel(kit: Kit, depth: number): THREE.Group {
  const g = new THREE.Group();
  const w = 6;
  g.add(at(kit.box(w + 0.4, 0.3, depth + 0.4, '#8d95a3', 0.08), w / 2, 0.15, 0));
  g.add(at(kit.box(w, 4.2, depth, '#fff1d6', 0.25), w / 2, 2.4, 0));
  g.add(at(kit.box(w + 0.6, 0.5, depth + 0.6, '#e2453c', 0.2), w / 2, 4.7, 0));
  g.add(at(kit.box(w - 1, 0.6, depth - 1, '#c9392f', 0.2), w / 2, 5.2, 0));
  // Awning over the belt entrances: red and white stripes along the wall.
  const stripes = Math.max(4, Math.round(depth / 0.8));
  const sw = depth / stripes;
  for (let i = 0; i < stripes; i++) {
    const s = kit.box(1.6, 0.14, sw, i % 2 ? '#ffffff' : '#e2453c', 0.04);
    s.position.set(-0.55, 3.55, -depth / 2 + sw * (i + 0.5));
    s.rotation.z = 0.35;
    g.add(s);
  }
  // A door and windows on the front.
  g.add(at(kit.box(1.4, 2.4, 0.16, '#7a4b2a', 0.1), w / 2, 1.5, depth / 2 + 0.02));
  for (const x of [1.2, w - 1.2])
    g.add(
      at(
        kit.box(1.2, 1.0, 0.14, '#7cc6f2', 0.08, { roughness: 0.1, emissive: '#1d4a66' }),
        x,
        2.6,
        depth / 2 + 0.02,
      ),
    );
  // Coins out front.
  const coins = kit.plastic('#ffcc33', { metalness: 0.35, roughness: 0.35 });
  const coinGeo = kit.geo('coin', () => new THREE.CylinderGeometry(0.32, 0.32, 0.12, 18));
  for (let i = 0; i < 9; i++) {
    const c = kit.mesh(coinGeo, coins);
    const stack = i % 3;
    c.position.set(w - 1.4 + stack * 0.7, 0.36 + Math.floor(i / 3) * 0.13, depth / 2 + 0.9);
    g.add(c);
  }
  return bake(kit, g);
}

/** Concrete slabs, two by two to a texture: each a little lighter or darker than the next,
 * with specks and darker seams between them. One slab is 4 world units wide. */
function concreteTexture(): THREE.CanvasTexture {
  const rnd = mulberry(11);
  return canvasTexture(512, 512, (c) => {
    const tints = ['#d3d6da', '#cdd1d6', '#d0d3d8', '#c9cdd3'];
    for (let i = 0; i < 4; i++) {
      const [x, y] = [(i % 2) * 256, Math.floor(i / 2) * 256];
      c.fillStyle = tints[i];
      c.fillRect(x, y, 256, 256);
      for (let k = 0; k < 260; k++) {
        c.fillStyle = rnd() < 0.5 ? 'rgba(90,96,108,0.10)' : 'rgba(255,255,255,0.22)';
        const s = 1 + rnd() * 2.5;
        c.fillRect(x + rnd() * 256, y + rnd() * 256, s, s);
      }
      c.fillStyle = '#b2b7bf';
      c.fillRect(x, y, 256, 3);
      c.fillRect(x, y, 3, 256);
    }
  });
}

/** The hall around the line: a concrete floor with aisle lines, loading bays under the
 * trucks and a hazard edge; a back wall with windows, a loading door, a sign and red steel
 * columns; crates and barrels along it; and outside, a street with lamps, hedges and a
 * passing car, a yard with a forklift, trees, a lawn with flowers, hills and clouds.
 * `bays` are the trucks' positions along the left edge. */
export function hallModel(kit: Kit, r: Rect, bays: number[] = []): THREE.Group {
  const g = new THREE.Group();
  const w = r.x1 - r.x0;
  const d = r.z1 - r.z0;
  const cx = (r.x0 + r.x1) / 2;
  const cz = (r.z0 + r.z1) / 2;
  const rnd = mulberry(7);
  const flat = (len: number, wid: number, color: string, x: number, y: number, z: number) =>
    at(kit.box(len, 0.04, wid, color, 0.01, { roughness: 0.8 }), x, y, z);

  const grass = kit.mesh(
    new THREE.PlaneGeometry(600, 600),
    kit.plastic('#79c25a', { roughness: 0.95 }),
    false,
  );
  kit.own(grass.geometry);
  grass.rotation.x = -Math.PI / 2;
  grass.position.set(cx, -0.42, cz);
  g.add(grass);

  // The floor: a concrete slab on a darker foundation lip.
  g.add(at(kit.box(w + 0.8, 0.3, d + 0.8, '#9299a3', 0.06), cx, -0.27, cz));
  g.add(at(kit.box(w, 0.4, d, '#c9cdd3', 0.1, { roughness: 0.45 }), cx, -0.2, cz));
  const slabs = kit.own(concreteTexture());
  slabs.repeat.set(w / 8, d / 8);
  const top = kit.mesh(
    kit.own(new THREE.PlaneGeometry(w - 0.2, d - 0.2)),
    kit.own(new THREE.MeshStandardMaterial({ map: slabs, roughness: 0.6 })),
    false,
  );
  top.rotation.x = -Math.PI / 2;
  top.position.set(cx, 0.005, cz);
  g.add(top);

  // Painted lines: a yellow aisle along the back wall and along the front, and a white bay
  // with a wheel stop under each truck.
  for (const z of [r.z0 + 2.9, r.z0 + 3.1, r.z1 - 1.35, r.z1 - 1.15])
    g.add(flat(w - 1.6, 0.1, '#f2c230', cx, 0.02, z));
  for (const z of bays) {
    const [bx0, bx1] = [r.x0 + 0.9, r.x0 + 8.6];
    for (const dz of [-1.75, 1.75])
      g.add(flat(bx1 - bx0, 0.18, '#f2c230', (bx0 + bx1) / 2, 0.02, z + dz));
    g.add(flat(0.18, 3.5, '#f2c230', bx0, 0.02, z));
    g.add(at(kit.box(0.35, 0.2, 2.2, '#f2c230', 0.06), bx0 + 0.6, 0.1, z));
  }

  // Hazard stripes round the edge of the floor.
  const hazard = kit.own(
    canvasTexture(128, 32, (c) => {
      c.fillStyle = '#f5c518';
      c.fillRect(0, 0, 128, 32);
      c.fillStyle = '#23262e';
      for (let x = -32; x < 160; x += 32) {
        c.beginPath();
        c.moveTo(x, 32);
        c.lineTo(x + 16, 32);
        c.lineTo(x + 32, 0);
        c.lineTo(x + 16, 0);
        c.fill();
      }
    }),
  );
  const edge = (len: number, x: number, z: number, along: boolean) => {
    const t = kit.own(hazard.clone());
    t.repeat.set(len / 2, 1);
    t.needsUpdate = true;
    const mat = kit.own(new THREE.MeshStandardMaterial({ map: t, roughness: 0.7 }));
    const s = kit.mesh(kit.own(new THREE.BoxGeometry(len, 0.08, 0.6)), mat, false);
    s.position.set(x, 0.04, z);
    if (!along) s.rotation.y = Math.PI / 2;
    g.add(s);
  };
  edge(w, cx, r.z1 - 0.3, true);
  edge(d, r.x0 + 0.3, cz, false);
  edge(d, r.x1 - 0.3, cz, false);

  // Back wall: blue-grey ribbed panels, a row of windows, red columns and a beam on top.
  const wallZ = r.z0 - 0.3;
  const wallH = 9;
  g.add(at(kit.box(w + 1, wallH, 0.6, '#8fa3b8', 0.1), cx, wallH / 2 - 0.4, wallZ));
  g.add(at(kit.box(w + 1.2, 1.2, 0.8, '#6f8399', 0.1), cx, 0.2, wallZ + 0.1));
  const ribs = Math.floor(w / 1.5);
  for (let i = 0; i <= ribs; i++)
    g.add(at(kit.box(0.24, wallH - 1.4, 0.2, '#a3b5c7', 0.06), r.x0 + i * 1.5, 4.6, wallZ + 0.36));
  const bay = 8;
  // The loading door: a ribbed shutter in the second bay, framed in hazard yellow.
  const doorX = r.x0 + bay * 1.5;
  for (let y = 0.4; y < 5; y += 0.42)
    g.add(at(kit.box(5.4, 0.36, 0.2, '#7d8794', 0.06), doorX, y, wallZ + 0.42));
  for (const dx of [-2.9, 2.9])
    g.add(at(kit.box(0.4, 5.2, 0.45, '#f2c230', 0.08), doorX + dx, 2.6, wallZ + 0.5));
  g.add(at(kit.box(6.2, 0.45, 0.5, '#f2c230', 0.08), doorX, 5.25, wallZ + 0.5));
  for (let x = r.x0 + bay / 2; x < r.x1 - 1; x += bay) {
    g.add(
      at(
        kit.box(5, 2, 0.3, '#dceeff', 0.12, { emissive: '#6aa8d8', roughness: 0.15 }),
        x,
        6,
        wallZ + 0.45,
      ),
    );
    g.add(at(kit.box(0.14, 2, 0.34, '#5d6e80', 0.03), x, 6, wallZ + 0.5));
    g.add(at(kit.box(5.4, 0.3, 0.5, '#5d6e80', 0.08), x, 4.9, wallZ + 0.5));
  }
  // A pipe run along the wall, with a bracket at every rib it passes.
  const pipe = kit.cylinder(0.16, 0.16, w - 2, '#d9dde2', 12);
  pipe.rotation.z = Math.PI / 2;
  g.add(at(pipe, cx, 7.6, wallZ + 0.62));
  for (let x = r.x0 + 3; x < r.x1 - 2; x += 4.5)
    g.add(at(kit.box(0.18, 0.5, 0.36, '#6f8399', 0.04), x, 7.6, wallZ + 0.5));
  let k = 0;
  for (let x = r.x0; x <= r.x1 + 0.01; x += bay, k++) {
    g.add(at(kit.box(0.9, wallH + 0.4, 0.9, '#d6453b', 0.12), x, wallH / 2 - 0.2, wallZ + 0.6));
    g.add(at(kit.box(1.3, 0.3, 1.3, '#a83229', 0.08), x, 0.15, wallZ + 0.6));
    // A fire extinguisher on every other column.
    if (k % 2 && x > r.x0 && x < r.x1) {
      g.add(at(kit.cylinder(0.2, 0.2, 0.8, '#e0312b', 12), x, 1.5, wallZ + 1.25));
      g.add(at(kit.box(0.16, 0.18, 0.16, '#23262e', 0.03), x, 2.0, wallZ + 1.25));
    }
  }
  g.add(at(kit.box(w + 2, 0.9, 1.1, '#d6453b', 0.14), cx, wallH, wallZ + 0.6));

  // On the roof edge: the sign in the middle, and air units and vents along the rest.
  const signW = Math.min(18, w * 0.4);
  const sign = kit.own(
    canvasTexture(1024, 160, (c) => {
      c.fillStyle = '#fff6df';
      c.fillRect(0, 0, 1024, 160);
      c.fillStyle = '#d6453b';
      c.textAlign = 'center';
      c.textBaseline = 'middle';
      c.font = "600 112px Fredoka, ui-rounded, 'Arial Rounded MT Bold', sans-serif";
      c.fillText('TOY WORKS', 512, 86);
    }),
  );
  sign.wrapS = sign.wrapT = THREE.ClampToEdgeWrapping;
  const face = kit.mesh(
    kit.own(new THREE.PlaneGeometry(signW, signW * (160 / 1024))),
    kit.own(new THREE.MeshStandardMaterial({ map: sign, roughness: 0.6 })),
    false,
  );
  const signY = wallH + 2.4;
  g.add(at(face, cx, signY, wallZ + 0.86));
  g.add(
    at(
      kit.box(signW + 0.6, signW * (160 / 1024) + 0.6, 0.5, '#23262e', 0.12),
      cx,
      signY,
      wallZ + 0.6,
    ),
  );
  for (const dx of [-signW / 3, signW / 3])
    g.add(at(kit.box(0.3, 1.6, 0.3, '#5d6e80', 0.04), cx + dx, wallH + 0.9, wallZ + 0.5));
  for (let x = r.x0 + 4; x < r.x1 - 3; x += 9) {
    if (Math.abs(x - cx) < signW / 2 + 2.5) continue;
    if (rnd() < 0.5) {
      g.add(at(kit.box(2.4, 1.3, 1.6, '#c6ccd4', 0.14), x, wallH + 1.1, wallZ + 0.1));
      const fan = kit.cylinder(0.5, 0.5, 0.08, '#5d6e80', 16);
      g.add(at(fan, x, wallH + 1.8, wallZ + 0.1));
    } else {
      g.add(at(kit.cylinder(0.3, 0.3, 1.4, '#c6ccd4', 12), x, wallH + 1.1, wallZ));
      g.add(at(kit.cylinder(0.5, 0.4, 0.3, '#9aa3ae', 12), x, wallH + 1.9, wallZ));
    }
  }

  // Crates, barrels and cones along the wall and in the corners.
  for (let x = r.x0 + 2.5; x < r.x1 - 2; x += bay) {
    if (rnd() < 0.35 || Math.abs(x - doorX) < 3.5) continue;
    const kind = rnd();
    if (kind < 0.5) {
      const c = ['#e2453c', '#2f7de1', '#36b55a'][Math.floor(rnd() * 3)];
      for (let n = 0; n < 3; n++)
        g.add(
          barrel(kit, c, x + (n % 2) * 1.1 + rnd() * 0.2, 0, wallZ + 1.6 + Math.floor(n / 2) * 1.1),
        );
    } else {
      g.add(pallet(kit, x + 0.7, 0, wallZ + 1.9));
      g.add(crate(kit, '#c9925a', x, 1.1, wallZ + 1.8));
      if (rnd() < 0.6) g.add(crate(kit, '#b98450', x + 1.5, 1.1, wallZ + 1.9));
      if (rnd() < 0.5) g.add(crate(kit, '#d6a06a', x + 0.6, 2.5, wallZ + 1.8));
    }
  }
  for (const [x, z] of [
    [r.x0 + 1.2, r.z1 - 1.4],
    [r.x1 - 1.2, r.z1 - 1.4],
    [r.x1 - 2.4, r.z1 - 1.4],
  ])
    g.add(cone(kit, x, z));

  // The street in front: a kerb and pavement, lamps, hedges and a bench, then the road.
  const [walkZ, roadZ] = [r.z1 + 2.2, r.z1 + 7.2];
  const sx0 = r.x0 - 40;
  const sx1 = r.x1 + 40;
  const sw = sx1 - sx0;
  const scx = (sx0 + sx1) / 2;
  g.add(at(kit.box(sw, 0.3, 3.2, '#d9d4c7', 0.04, { roughness: 0.9 }), scx, -0.27, walkZ));
  for (let x = sx0; x < sx1; x += 2) g.add(flat(0.06, 3.2, '#bcb6a6', x, -0.11, walkZ));
  g.add(at(kit.box(sw, 0.36, 0.4, '#a7a294', 0.04), scx, -0.24, walkZ + 1.8));
  g.add(at(kit.box(sw, 0.1, 6.4, '#4c525c', 0.02, { roughness: 0.95 }), scx, -0.37, roadZ));
  for (const dz of [-2.9, 2.9]) g.add(flat(sw, 0.12, '#e9e6dc', scx, -0.31, roadZ + dz));
  for (let x = sx0 + 1; x < sx1; x += 4) g.add(flat(2, 0.16, '#f2c230', x, -0.31, roadZ));
  for (let x = r.x0 + 2; x < r.x1 + 1; x += 12) g.add(lampPost(kit, x, walkZ - 0.9));
  for (let x = r.x0 + 6; x < r.x1 - 4; x += 12) {
    const len = 3.2 + rnd() * 2;
    g.add(at(kit.box(len, 0.8, 0.9, '#3f9d3a', 0.3), x + len / 2, 0.0, r.z1 + 0.95));
    for (let f = 0; f < 4; f++)
      g.add(
        at(
          kit.box(0.2, 0.2, 0.2, ['#ff7ac8', '#ffd84a', '#ffffff'][f % 3], 0.08),
          x + 0.5 + rnd() * (len - 1),
          0.45,
          r.z1 + 0.6 + rnd() * 0.7,
        ),
      );
  }
  g.add(bench(kit, cx + 6, walkZ + 0.5));
  g.add(hydrant(kit, r.x0 + 8, walkZ + 0.9));
  // A toy car that drives along the road, and back, on the far side.
  const car = carModel(kit, '#2f7de1');
  car.userData['live'] = true;
  // Shadows are drawn once, when the hall is built, so a moving car casts none.
  car.traverse((o) => (o.castShadow = false));
  g.add(at(car, cx, -0.32, roadZ + 1.5));
  if (kit.motion) kit.tickers.push((t) => (car.position.x = sx0 + 10 + ((t * 4) % (sw - 20))));
  g.add(at(carModel(kit, '#ffb020'), r.x1 - 6, -0.32, roadZ - 1.5));

  // The yard right of the hall: a paved pad with a forklift, pallets and a skip.
  const [yx0, yx1] = [r.x1 + 1.2, r.x1 + 10];
  const [yz0, yz1] = [r.z1 - 13, r.z1 + 0.6];
  const pad = kit.box(yx1 - yx0, 0.1, yz1 - yz0, '#8e949d', 0.02, { roughness: 0.95 });
  g.add(at(pad, (yx0 + yx1) / 2, -0.37, (yz0 + yz1) / 2));
  for (let z = yz0 + 3; z < yz1 - 1; z += 3.4) g.add(flat(3.2, 0.12, '#f7f7f2', yx1 - 2, -0.31, z));
  g.add(forklift(kit, yx0 + 3.2, yz1 - 3.5));
  g.add(pallet(kit, yx1 - 2, -0.32, yz0 + 4.7));
  g.add(crate(kit, '#c9925a', yx1 - 2, 0.78, yz0 + 4.7));
  g.add(pallet(kit, yx1 - 2, -0.32, yz0 + 8.1));
  g.add(pallet(kit, yx1 - 2, 0.08, yz0 + 8.1));
  g.add(at(kit.box(3.4, 1.6, 2, '#36b55a', 0.16), yx0 + 2.6, 0.4, yz0 + 1.8));
  g.add(at(kit.box(3.6, 0.2, 2.2, '#2a8a46', 0.08), yx0 + 2.6, 1.25, yz0 + 1.8));

  // Trees in loose rows: thick behind the hall, thinner down each side, a few past the
  // street; round ones, pines and tall poplars.
  const plant = (x: number, z: number) => g.add(tree(kit, rnd, x, z));
  for (let row = 0; row < 3; row++)
    for (let x = r.x0 - 16 + row * 2.5; x < r.x1 + 18; x += 5.5)
      if (rnd() > 0.22) plant(x + (rnd() - 0.5) * 2.5, r.z0 - 5 - row * 6.5 - rnd() * 2.5);
  for (let z = r.z0; z < r.z1 + 2; z += 6) {
    if (rnd() > 0.25) plant(r.x0 - 7 - rnd() * 2, z + rnd() * 2);
    if (rnd() > 0.5) plant(r.x0 - 13 - rnd() * 3, z + rnd() * 3);
    if (rnd() > 0.3) plant(yx1 + 3 + rnd() * 2, z + rnd() * 2);
    if (rnd() > 0.5) plant(yx1 + 9 + rnd() * 3, z + rnd() * 3);
  }
  for (let x = sx0 + 8; x < sx1 - 6; x += 9)
    if (rnd() > 0.45) plant(x + rnd() * 3, roadZ + 8 + rnd() * 5);

  // The lawn: tufts of darker grass, flowers and pebbles, more of them near the hall.
  const dots = [
    ['#5fa844', 0.4, 0.25],
    ['#4e9a3a', 0.3, 0.35],
    ['#ffffff', 0.18, 0.18],
    ['#ffd84a', 0.18, 0.18],
    ['#ff7ac8', 0.18, 0.18],
    ['#b9b4aa', 0.35, 0.16],
  ] as const;
  for (let i = 0; i < 320; i++) {
    const [x, z] =
      i % 3 === 0
        ? [sx0 + rnd() * sw, roadZ + 4 + rnd() * 16]
        : i % 3 === 1
          ? [r.x0 - 4 - rnd() * 22, r.z0 - 4 + rnd() * (d + 8)]
          : [yx1 + 1 + rnd() * 22, r.z0 - 4 + rnd() * (d + 8)];
    const [c, s, h] = dots[Math.floor(rnd() * dots.length)];
    g.add(at(kit.box(s, h, s, c, 0.05), x, -0.42 + h / 2, z));
  }

  const hillGeo = kit.geo('hill', () => new THREE.SphereGeometry(1, 32, 16));
  for (let i = 0; i < 7; i++) {
    const hill = kit.mesh(
      hillGeo,
      kit.plastic(['#5fae45', '#6bbb4e', '#56a03f'][i % 3], { roughness: 0.95 }),
      false,
    );
    const rad = 28 + rnd() * 26;
    hill.scale.set(rad * 1.6, rad * (0.45 + rnd() * 0.25), rad);
    hill.position.set(cx - 130 + i * 45 + rnd() * 20, -0.4, r.z0 - 70 - rnd() * 40);
    g.add(hill);
  }

  // Clouds drifting in the sky behind the hall.
  for (let i = 0; i < 9; i++) {
    const cloud = cloudModel(kit, rnd);
    cloud.userData['live'] = true;
    const y = 26 + rnd() * 14;
    const z = r.z0 - 50 - rnd() * 50;
    const x0 = cx - 110 + rnd() * 220;
    cloud.position.set(x0, y, z);
    cloud.scale.setScalar(2.2 + rnd() * 2.2);
    g.add(cloud);
    if (kit.motion)
      kit.tickers.push((t) => (cloud.position.x = cx - 120 + ((x0 - cx + 120 + t * 0.8) % 240)));
  }
  return bake(kit, g);
}

/** A wooden pallet. */
function pallet(kit: Kit, x: number, y: number, z: number): THREE.Group {
  const g = new THREE.Group();
  for (const dz of [-0.6, 0, 0.6]) g.add(at(kit.box(1.7, 0.18, 0.3, '#b98a55', 0.03), 0, 0.3, dz));
  for (const dx of [-0.7, 0, 0.7]) g.add(at(kit.box(0.3, 0.2, 1.6, '#9c7243', 0.03), dx, 0.1, 0));
  return at(g, x, y, z);
}

/** A street lamp: a chunky green post with a round, warm light on top. */
function lampPost(kit: Kit, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(0.7, 0.4, 0.7, '#2f5f4a', 0.12), 0, 0.2, 0));
  g.add(at(kit.cylinder(0.17, 0.2, 3.6, '#2f5f4a', 12), 0, 2.2, 0));
  g.add(at(kit.cylinder(0.32, 0.22, 0.3, '#2f5f4a', 14), 0, 4.1, 0));
  const glow = kit.mesh(
    kit.geo('lamp-globe', () => new THREE.SphereGeometry(0.42, 16, 12)),
    kit.plastic('#fff3c4', { emissive: '#ffd77a', roughness: 0.3 }),
  );
  g.add(at(glow, 0, 4.55, 0));
  g.add(at(kit.cylinder(0.08, 0.36, 0.22, '#2f5f4a', 14), 0, 4.98, 0));
  return at(g, x, -0.12, z);
}

function bench(kit: Kit, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  for (const dz of [-0.2, 0.15]) g.add(at(kit.box(2.6, 0.12, 0.3, '#c47a3c', 0.04), 0, 0.6, dz));
  g.add(at(kit.box(2.6, 0.5, 0.12, '#c47a3c', 0.04), 0, 1.0, 0.36));
  for (const dx of [-1.1, 1.1]) g.add(at(kit.box(0.14, 0.6, 0.7, '#3d4350', 0.03), dx, 0.3, 0.05));
  return at(g, x, -0.12, z);
}

function hydrant(kit: Kit, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.cylinder(0.26, 0.3, 0.9, '#e0312b', 12), 0, 0.45, 0));
  g.add(at(kit.cylinder(0.08, 0.3, 0.3, '#e0312b', 12), 0, 1.05, 0));
  const arm = kit.cylinder(0.1, 0.1, 0.8, '#b8241f', 8);
  arm.rotation.z = Math.PI / 2;
  g.add(at(arm, 0, 0.6, 0));
  return at(g, x, -0.12, z);
}

/** A little toy car facing right. */
function carModel(kit: Kit, color: string): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(3, 0.8, 1.6, color, 0.3), 0, 0.75, 0));
  g.add(at(kit.box(1.7, 0.7, 1.4, shade(color, 0.12), 0.28), -0.2, 1.4, 0));
  g.add(at(kit.box(1.5, 0.5, 1.44, '#9fd3f2', 0.2, { roughness: 0.1 }), -0.2, 1.42, 0));
  for (const dx of [-0.95, 0.95])
    for (const dz of [-0.75, 0.75]) {
      const wh = kit.cylinder(0.36, 0.36, 0.3, '#262a31', 14);
      wh.rotation.x = Math.PI / 2;
      g.add(at(wh, dx, 0.38, dz));
    }
  g.add(at(kit.box(0.1, 0.2, 0.4, '#fff6c2', 0.04, { emissive: '#ffe680' }), 1.5, 0.85, 0.5));
  g.add(at(kit.box(0.1, 0.2, 0.4, '#fff6c2', 0.04, { emissive: '#ffe680' }), 1.5, 0.85, -0.5));
  return bake(kit, g);
}

/** A yellow forklift with its forks down, facing the hall. */
function forklift(kit: Kit, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(2.2, 0.9, 1.6, '#f2b630', 0.2), 0, 0.85, 0));
  g.add(at(kit.box(0.9, 0.9, 1.5, '#3d4350', 0.2), 0.7, 0.9, 0));
  g.add(at(kit.box(0.6, 0.6, 1.0, '#23262e', 0.12), 0.0, 1.55, 0));
  for (const dz of [-0.65, 0.65]) {
    g.add(at(kit.box(0.12, 1.9, 0.12, '#3d4350', 0.03), -0.6, 2.3, dz));
    g.add(at(kit.box(0.14, 3.0, 0.16, '#23262e', 0.03), -1.25, 1.7, dz * 0.7));
    g.add(at(kit.box(1.3, 0.08, 0.2, '#9aa0aa', 0.02), -1.9, 0.25, dz * 0.5));
  }
  g.add(at(kit.box(1.5, 0.12, 1.6, '#f2b630', 0.05), -0.05, 3.25, 0));
  for (const dx of [-0.6, 0.6])
    for (const dz of [-0.8, 0.8]) {
      const wh = kit.cylinder(0.4, 0.4, 0.34, '#262a31', 14);
      wh.rotation.x = Math.PI / 2;
      g.add(at(wh, dx, 0.4, dz));
    }
  g.rotation.y = 0.5;
  return at(g, x, -0.32, z);
}

function barrel(kit: Kit, color: string, x: number, y: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.cylinder(0.5, 0.5, 1.3, color), 0, 0.65, 0));
  for (const h of [0.3, 1.0])
    g.add(at(kit.cylinder(0.53, 0.53, 0.1, shade(color, -0.18)), 0, h, 0));
  g.add(at(kit.cylinder(0.46, 0.46, 0.04, shade(color, 0.1)), 0, 1.31, 0));
  return at(g, x, y, z);
}

/** A blocky tree: a round one (stacked leaf blocks), a pine (shrinking tiers) or a tall
 * poplar, on a brown trunk. */
function tree(kit: Kit, rnd: () => number, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  const kind = rnd();
  const h = 1.6 + rnd() * 1.4;
  g.add(at(kit.box(0.7, h, 0.7, '#8a5a35', 0.1), 0, h / 2, 0));
  if (kind < 0.55) {
    const greens = ['#3f9d3a', '#4cb043', '#2f8a34', '#5cbf4a'];
    const c = greens[Math.floor(rnd() * greens.length)];
    const s = 2.6 + rnd() * 1.2;
    g.add(at(kit.box(s, s * 0.8, s, c, 0.45), 0, h + s * 0.35, 0));
    g.add(at(kit.box(s * 0.65, s * 0.55, s * 0.65, shade(c, 0.06), 0.35), 0.1, h + s * 0.95, -0.1));
  } else if (kind < 0.85) {
    const c = ['#2e7d4a', '#357f3c', '#2a7040'][Math.floor(rnd() * 3)];
    const s = 3 + rnd() * 0.8;
    for (let t = 0; t < 3; t++)
      g.add(
        at(
          kit.box(s * (1 - t * 0.28), 1.3, s * (1 - t * 0.28), shade(c, t * 0.04), 0.3),
          0,
          h * 0.6 + 0.6 + t * 1.15,
          0,
        ),
      );
  } else {
    const c = '#7cc44f';
    g.add(at(kit.box(1.8, 4.4, 1.8, c, 0.6), 0, h + 1.8, 0));
    g.add(at(kit.box(1.2, 1.2, 1.2, shade(c, 0.06), 0.4), 0, h + 4.3, 0));
  }
  // A slight turn each, so they look planted by hand without showing their corners.
  g.rotation.y = (rnd() - 0.5) * 0.5;
  return at(g, x, 0, z);
}

function cone(kit: Kit, x: number, z: number): THREE.Group {
  const g = new THREE.Group();
  g.add(at(kit.box(0.9, 0.12, 0.9, '#f26b1d', 0.04), 0, 0.06, 0));
  g.add(at(kit.cylinder(0.08, 0.38, 1.1, '#f26b1d', 16), 0, 0.65, 0));
  g.add(at(kit.cylinder(0.2, 0.27, 0.22, '#ffffff', 16), 0, 0.75, 0));
  return at(g, x, 0, z);
}

function cloudModel(kit: Kit, rnd: () => number): THREE.Group {
  const g = new THREE.Group();
  const geo = kit.geo('cloud', () => new THREE.IcosahedronGeometry(1, 2));
  const mat = kit.plastic('#ffffff', { roughness: 1, emissive: '#dde8f2' });
  const n = 4 + Math.floor(rnd() * 3);
  for (let i = 0; i < n; i++) {
    const s = new THREE.Mesh(geo, mat);
    s.position.set((i - n / 2) * 1.1, Math.sin(i * 1.7) * 0.3 + (i % 2 ? 0.5 : 0), rnd() * 0.6);
    s.scale.setScalar(0.9 + rnd() * 0.7);
    g.add(s);
  }
  return bake(kit, g);
}

/** A small seeded random generator, so the decor is the same every time. */
function mulberry(seed: number): () => number {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
