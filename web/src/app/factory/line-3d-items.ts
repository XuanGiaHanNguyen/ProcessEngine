import * as THREE from 'three';

import { Kit, bake, canvasTexture, shade } from './line-3d-models';

/** Little toy models of every resource, shown on the signs floating over the floor. */

const at = <T extends THREE.Object3D>(o: T, x: number, y: number, z: number): T => {
  o.position.set(x, y, z);
  return o;
};

function sphere(
  kit: Kit,
  r: number,
  color: THREE.ColorRepresentation,
  opts: THREE.MeshStandardMaterialParameters = {},
): THREE.Mesh {
  return kit.mesh(
    kit.geo(`sphere|${r}`, () => new THREE.SphereGeometry(r, 20, 14)),
    kit.plastic(color, opts),
  );
}

function torus(kit: Kit, r: number, tube: number, color: THREE.ColorRepresentation): THREE.Mesh {
  return kit.mesh(
    kit.geo(`torus|${r}|${tube}`, () => new THREE.TorusGeometry(r, tube, 12, 32)),
    kit.plastic(color),
  );
}

/** A cylinder lying along x, or facing the front along z. */
function lying(m: THREE.Mesh, axis: 'x' | 'z'): THREE.Mesh {
  if (axis === 'x') m.rotation.z = Math.PI / 2;
  else m.rotation.x = Math.PI / 2;
  return m;
}

function cotton(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.cylinder(0.07, 0.09, 0.9, '#5a8f3a', 8), 0, 0.45, 0));
  for (const s of [-1, 1]) {
    const leaf = sphere(kit, 0.3, '#4cb043');
    leaf.scale.set(1, 0.35, 0.6);
    g.add(at(leaf, s * 0.3, 0.5, 0));
  }
  for (const [x, y, z, r] of [
    [0, 1.15, 0, 0.36],
    [-0.32, 1.0, 0.1, 0.28],
    [0.32, 1.0, 0.1, 0.28],
    [0, 1.0, -0.3, 0.28],
    [0, 1.42, 0.05, 0.22],
  ])
    g.add(at(sphere(kit, r, '#fbfbf8', { roughness: 0.95 }), x, y, z));
}

function spool(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.cylinder(0.6, 0.6, 0.16, '#c79a6b'), 0, 0.08, 0));
  g.add(at(kit.cylinder(0.6, 0.6, 0.16, '#c79a6b'), 0, 1.42, 0));
  g.add(at(kit.cylinder(0.46, 0.46, 1.18, '#f4a3c0'), 0, 0.75, 0));
  for (const y of [0.4, 0.75, 1.1]) g.add(at(torus(kit, 0.46, 0.03, '#e27ea4'), 0, y, 0));
  g.children.slice(-3).forEach((r) => (r.rotation.x = Math.PI / 2));
}

function bottle(kit: Kit, g: THREE.Group): void {
  const blue = '#7cc6f2';
  g.add(at(kit.cylinder(0.45, 0.45, 1.1, blue), 0, 0.55, 0));
  g.add(at(kit.cylinder(0.465, 0.465, 0.42, '#ffffff'), 0, 0.6, 0));
  g.add(at(kit.cylinder(0.2, 0.45, 0.35, blue), 0, 1.27, 0));
  g.add(at(kit.cylinder(0.18, 0.18, 0.2, blue), 0, 1.53, 0));
  g.add(at(kit.cylinder(0.23, 0.23, 0.2, '#2f7de1'), 0, 1.72, 0));
}

function ball(kit: Kit, g: THREE.Group): void {
  g.add(at(sphere(kit, 0.72, '#f2a65a', { roughness: 0.4 }), 0, 0.72, 0));
  const band = at(torus(kit, 0.72, 0.09, '#e2453c'), 0, 0.72, 0);
  band.rotation.y = 0.5;
  g.add(band);
}

function logs(kit: Kit, g: THREE.Group): void {
  for (const [y, z] of [
    [0.32, -0.33],
    [0.32, 0.33],
    [0.88, 0],
  ]) {
    g.add(at(lying(kit.cylinder(0.32, 0.32, 1.7, '#8a5a35', 14), 'x'), 0, y, z));
    for (const x of [-0.86, 0.86]) {
      g.add(at(lying(kit.cylinder(0.27, 0.27, 0.04, '#e0b07a', 14), 'x'), x, y, z));
      g.add(at(lying(kit.cylinder(0.13, 0.13, 0.05, '#c79a6b', 10), 'x'), x, y, z));
    }
  }
}

function bucket(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.cylinder(0.58, 0.46, 1.0, '#b9bec6'), 0, 0.5, 0));
  g.add(at(kit.cylinder(0.53, 0.53, 0.06, '#ff7ac8'), 0, 0.98, 0));
  g.add(at(kit.box(0.16, 0.45, 0.08, '#ff7ac8', 0.04), 0.2, 0.78, 0.55));
  g.add(at(sphere(kit, 0.1, '#ff7ac8'), 0.2, 0.55, 0.56));
  g.add(at(torus(kit, 0.58, 0.04, '#5d6e80'), 0, 1.0, 0));
}

function cloth(kit: Kit, g: THREE.Group): void {
  ['#c9a7f2', '#b48fe6', '#c9a7f2'].forEach((c, i) => {
    const fold = kit.box(1.4, 0.24, 1.0, c, 0.1);
    fold.rotation.y = (i - 1) * 0.15;
    g.add(at(fold, 0, 0.13 + i * 0.25, 0));
  });
}

function fluff(kit: Kit, g: THREE.Group): void {
  for (const [x, y, z, r] of [
    [0, 0.55, 0, 0.5],
    [-0.45, 0.4, 0.1, 0.38],
    [0.45, 0.42, 0.05, 0.4],
    [0.1, 0.4, -0.4, 0.36],
    [-0.1, 0.95, 0.05, 0.34],
  ])
    g.add(at(sphere(kit, r, '#f7f7f5', { roughness: 1 }), x, y, z));
}

function guitarBody(kit: Kit, g: THREE.Group, color: string, y = 0): void {
  g.add(at(lying(kit.cylinder(0.62, 0.62, 0.28, color, 24), 'z'), 0, y + 0.66, 0));
  g.add(at(lying(kit.cylinder(0.46, 0.46, 0.28, color, 24), 'z'), 0, y + 1.32, 0));
  g.add(at(lying(kit.cylinder(0.18, 0.18, 0.06, '#2b2f36', 16), 'z'), 0, y + 1.05, 0.13));
  g.add(at(kit.box(0.5, 0.1, 0.06, shade(color, -0.25), 0.02), 0, y + 0.55, 0.15));
}

function guitar(kit: Kit, g: THREE.Group): void {
  guitarBody(kit, g, '#ff8c5a', -0.25);
  g.add(at(kit.box(0.18, 1.2, 0.1, '#7a4b2a', 0.03), 0, 1.75, 0));
  g.add(at(kit.box(0.3, 0.38, 0.12, '#5c371d', 0.05), 0, 2.48, 0));
  for (const s of [-1, 1]) g.add(at(sphere(kit, 0.06, '#f5c518'), s * 0.2, 2.48, 0));
  g.scale.setScalar(0.8);
}

function chips(kit: Kit, g: THREE.Group): void {
  [
    [0, 0.1, 0, 0.3, '#c79a6b'],
    [0.4, 0.1, 0.2, -0.5, '#b07b4a'],
    [-0.35, 0.1, 0.25, 0.9, '#d8b07a'],
    [0.05, 0.3, 0.05, 1.4, '#e0b07a'],
  ].forEach(([x, y, z, r, c]) => {
    const chip = kit.box(0.7, 0.18, 0.4, c as string, 0.05);
    chip.rotation.y = r as number;
    g.add(at(chip, x as number, y as number, z as number));
  });
}

function dowels(kit: Kit, g: THREE.Group): void {
  for (const [x, z] of [
    [0, 0],
    [0.24, 0],
    [-0.24, 0],
    [0.12, 0.2],
    [-0.12, 0.2],
    [0.12, -0.2],
    [-0.12, -0.2],
  ])
    g.add(at(kit.cylinder(0.11, 0.11, 1.7, '#d8c08c', 10), x, 0.85, z));
  for (const y of [0.45, 1.25]) {
    const band = at(torus(kit, 0.36, 0.05, '#e2453c'), 0, y, 0);
    band.rotation.x = Math.PI / 2;
    g.add(band);
  }
}

function pegs(kit: Kit, g: THREE.Group): void {
  for (const [x, z] of [
    [-0.35, 0],
    [0.35, 0],
    [0, 0.3],
    [0, -0.3],
  ]) {
    g.add(at(kit.cylinder(0.08, 0.08, 0.8, '#d0d4da', 10), x, 0.4, z));
    g.add(at(kit.box(0.34, 0.26, 0.12, '#f26b6b', 0.05), x, 0.9, z));
  }
}

function wheel(kit: Kit, g: THREE.Group, x = 0, z = 0): void {
  g.add(at(torus(kit, 0.45, 0.24, '#262a31'), x, 0.7, z));
  g.add(at(lying(kit.cylinder(0.3, 0.3, 0.3, '#c0c6d0', 16), 'z'), x, 0.7, z));
  g.add(at(lying(kit.cylinder(0.1, 0.1, 0.36, '#f5c518', 10), 'z'), x, 0.7, z));
}

function chassis(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.box(1.7, 0.16, 0.9, '#b0b8c8', 0.05), 0, 0.5, 0));
  g.add(at(kit.box(1.2, 0.12, 0.5, '#8d95a3', 0.04), 0, 0.64, 0));
  for (const x of [-0.6, 0.6]) {
    g.add(at(lying(kit.cylinder(0.06, 0.06, 1.2, '#4a5160', 8), 'z'), x, 0.36, 0));
    for (const z of [-0.6, 0.6])
      g.add(at(lying(kit.cylinder(0.26, 0.26, 0.16, '#262a31', 16), 'z'), x, 0.3, z));
  }
}

function gear(kit: Kit, g: THREE.Group, color = '#d4d9e0', y = 0.95, r = 0.62): void {
  g.add(at(lying(kit.cylinder(r, r, 0.24, color, 24), 'z'), 0, y, 0));
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2;
    const tooth = kit.box(0.24, 0.24, 0.24, color, 0.04);
    tooth.rotation.z = a;
    g.add(at(tooth, Math.cos(a) * (r + 0.12), y + Math.sin(a) * (r + 0.12), 0));
  }
  g.add(at(lying(kit.cylinder(0.2, 0.2, 0.32, '#5d6e80', 12), 'z'), 0, y, 0));
}

function motor(kit: Kit, g: THREE.Group): void {
  g.add(at(lying(kit.cylinder(0.45, 0.45, 1.1, '#5a8dee', 20), 'x'), 0, 0.55, 0));
  for (const x of [-0.58, 0.58])
    g.add(at(lying(kit.cylinder(0.38, 0.38, 0.08, '#3b4fb8', 20), 'x'), x, 0.55, 0));
  g.add(at(lying(kit.cylinder(0.08, 0.08, 0.3, '#d0d4da', 8), 'x'), 0.78, 0.55, 0));
  g.add(at(kit.cylinder(0.07, 0.07, 0.45, '#d0d4da', 8), 0, 1.15, 0));
  for (const s of [-1, 1]) g.add(at(torus(kit, 0.2, 0.07, '#f5c518'), s * 0.22, 1.5, 0));
}

function teddy(kit: Kit, g: THREE.Group): void {
  const fur = '#c98b4f';
  const light = '#f0c99a';
  g.add(at(sphere(kit, 0.55, fur), 0, 0.6, 0));
  g.add(at(sphere(kit, 0.34, light), 0, 0.55, 0.3));
  g.add(at(sphere(kit, 0.44, fur), 0, 1.35, 0));
  g.add(at(sphere(kit, 0.18, light), 0, 1.25, 0.38));
  g.add(at(sphere(kit, 0.06, '#1f2430'), 0, 1.3, 0.55));
  for (const s of [-1, 1]) {
    g.add(at(sphere(kit, 0.16, fur), s * 0.32, 1.72, 0));
    g.add(at(sphere(kit, 0.09, light), s * 0.32, 1.72, 0.08));
    g.add(at(sphere(kit, 0.055, '#1f2430'), s * 0.15, 1.45, 0.38));
    g.add(at(sphere(kit, 0.2, fur), s * 0.55, 0.8, 0.1));
    g.add(at(sphere(kit, 0.22, fur), s * 0.3, 0.18, 0.25));
  }
}

function car(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.box(1.7, 0.45, 0.85, '#ff6b6b', 0.15), 0, 0.5, 0));
  g.add(at(kit.box(0.85, 0.42, 0.75, '#ffe3e3', 0.12), -0.1, 0.92, 0));
  g.add(at(kit.box(0.87, 0.26, 0.77, '#7cc6f2', 0.08), -0.1, 0.94, 0));
  for (const x of [-0.55, 0.55])
    for (const z of [-0.43, 0.43])
      g.add(at(lying(kit.cylinder(0.24, 0.24, 0.16, '#262a31', 16), 'z'), x, 0.26, z));
}

function kite(kit: Kit, g: THREE.Group): void {
  const sail = kit.box(1.0, 1.0, 0.08, '#6be0c8', 0.04);
  sail.rotation.z = Math.PI / 4;
  g.add(at(sail, 0, 1.55, 0));
  const half = kit.box(0.7, 0.7, 0.09, '#ff7ac8', 0.03);
  half.rotation.z = Math.PI / 4;
  g.add(at(half, 0.25, 1.8, 0.01));
  g.add(at(kit.box(0.05, 1.4, 0.1, '#7a4b2a', 0.02), 0, 1.55, 0.02));
  for (let i = 0; i < 3; i++) {
    const bow = kit.box(0.24, 0.12, 0.06, ['#f5c518', '#e2453c', '#2f7de1'][i], 0.03);
    bow.rotation.z = 0.4 * (i % 2 ? 1 : -1);
    g.add(at(bow, 0.08 * (i % 2 ? 1 : -1), 0.65 - i * 0.22 + 0.2, 0));
  }
}

function robot(kit: Kit, g: THREE.Group): void {
  const metal = '#c0c8d8';
  g.add(at(kit.box(0.9, 0.8, 0.6, metal, 0.12), 0, 0.85, 0));
  g.add(at(kit.box(0.4, 0.3, 0.06, '#e2453c', 0.04), 0, 0.88, 0.31));
  g.add(at(kit.box(0.65, 0.5, 0.5, metal, 0.12), 0, 1.55, 0));
  g.add(at(kit.box(0.5, 0.18, 0.06, '#1f2430', 0.04), 0, 1.6, 0.26));
  for (const s of [-1, 1]) {
    g.add(
      at(
        sphere(kit, 0.06, '#5ff2ff', { emissive: '#5ff2ff', emissiveIntensity: 1 }),
        s * 0.12,
        1.6,
        0.3,
      ),
    );
    g.add(at(kit.box(0.2, 0.6, 0.22, shade(metal, -0.12), 0.06), s * 0.58, 0.85, 0));
    g.add(at(kit.box(0.24, 0.45, 0.3, shade(metal, -0.12), 0.06), s * 0.22, 0.22, 0));
  }
  g.add(at(kit.cylinder(0.03, 0.03, 0.3, '#5d6e80', 6), 0, 1.95, 0));
  g.add(
    at(sphere(kit, 0.09, '#e2453c', { emissive: '#e2453c', emissiveIntensity: 0.6 }), 0, 2.12, 0),
  );
}

function racer(kit: Kit, g: THREE.Group): void {
  const red = '#ff3b3b';
  g.add(at(kit.box(1.9, 0.32, 0.8, red, 0.12), 0, 0.42, 0));
  g.add(at(kit.box(0.6, 0.22, 0.5, '#1f2430', 0.1), -0.05, 0.66, 0));
  g.add(at(kit.box(0.2, 0.36, 0.06, '#1f2430', 0.02), -0.85, 0.72, 0.3));
  g.add(at(kit.box(0.2, 0.36, 0.06, '#1f2430', 0.02), -0.85, 0.72, -0.3));
  g.add(at(kit.box(0.34, 0.06, 0.9, red, 0.02), -0.85, 0.92, 0));
  g.add(at(lying(kit.cylinder(0.16, 0.16, 0.04, '#ffffff', 16), 'z'), 0.35, 0.44, 0.41));
  for (const x of [-0.6, 0.6])
    for (const z of [-0.45, 0.45])
      g.add(at(lying(kit.cylinder(0.28, 0.28, 0.22, '#262a31', 16), 'z'), x, 0.28, z));
}

function coin(kit: Kit, g: THREE.Group): void {
  const gold = { metalness: 0.45, roughness: 0.3 };
  g.add(at(lying(kit.cylinder(0.75, 0.75, 0.18, '#ffcc33', 28), 'z'), 0, 1.0, 0));
  g.add(
    at(
      lying(
        kit.mesh(
          kit.geo('coin-face', () => new THREE.CylinderGeometry(0.55, 0.55, 0.22, 28)),
          kit.plastic('#ffdc5e', gold),
        ),
        'z',
      ),
      0,
      1.0,
      0,
    ),
  );
  g.add(at(kit.box(0.14, 0.62, 0.26, '#e0a800', 0.04), 0, 1.0, 0));
}

function crate(kit: Kit, g: THREE.Group): void {
  g.add(at(kit.box(1.1, 1.1, 1.1, '#c9925a', 0.1), 0, 0.55, 0));
  g.add(at(kit.box(1.16, 0.16, 1.16, '#a7743f', 0.04), 0, 1.06, 0));
}

const MODELS: Record<string, (kit: Kit, g: THREE.Group) => void> = {
  cotton,
  string: spool,
  plastic: bottle,
  rubber: ball,
  wood: logs,
  paint: bucket,
  fabric: cloth,
  stuffing: fluff,
  'guitar-body': (kit, g) => guitarBody(kit, g, '#e0b07a'),
  'painted-body': (kit, g) => guitarBody(kit, g, '#e8556d'),
  offcut: chips,
  dowel: dowels,
  pegs,
  wheels: (kit, g) => wheel(kit, g),
  chassis,
  gears: (kit, g) => gear(kit, g),
  motor,
  'stuffed-animal': teddy,
  'toy-guitar': guitar,
  'toy-car': car,
  kite,
  robot,
  racer,
  market: coin,
};

/**
 * A sign floating over a node: a toy model of what it makes or holds (`market` for the
 * market's coin), turning slowly on a round white plate, with how many machines it stands
 * for when there are several.
 */
export function itemSign(kit: Kit, resource: string, count = 1): THREE.Group {
  const sign = new THREE.Group();
  const plate = new THREE.Group();
  plate.add(kit.cylinder(1.05, 1.05, 0.16, '#ffffff', 32));
  const rim = torus(kit, 1.05, 0.08, '#1f2430');
  rim.rotation.x = Math.PI / 2;
  plate.add(rim);
  sign.add(bake(kit, plate));

  const model = new THREE.Group();
  (MODELS[resource] ?? crate)(kit, model);
  bake(kit, model);
  model.position.y = 0.08;
  model.scale.multiplyScalar(1.4);
  model.userData['live'] = true;
  sign.add(model);
  if (kit.motion) kit.tickers.push((_, dt) => (model.rotation.y += dt * 0.6));

  if (count > 1) {
    const tex = kit.own(
      canvasTexture(128, 64, (c) => {
        c.fillStyle = '#1f2430';
        c.beginPath();
        c.roundRect(4, 4, 120, 56, 28);
        c.fill();
        c.fillStyle = '#ffffff';
        c.font = 'bold 40px Fredoka, ui-rounded, system-ui, sans-serif';
        c.textAlign = 'center';
        c.textBaseline = 'middle';
        c.fillText(`×${count}`, 64, 34);
      }),
    );
    tex.wrapS = tex.wrapT = THREE.ClampToEdgeWrapping;
    const badge = new THREE.Sprite(kit.own(new THREE.SpriteMaterial({ map: tex })));
    badge.scale.set(1.3, 0.65, 1);
    sign.add(at(badge, 1.15, 0.25, 0.6));
  }
  return sign;
}
