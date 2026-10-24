import { Component, computed, input } from '@angular/core';

import { Instance, Solution } from '../../api/models';
import { processColors } from '../../shared/palette';

interface Node {
  key: string;
  kind: 'resource' | 'process';
  id: string;
  label: string;
  sub: string;
  x: number;
  y: number;
  color: string;
  used: boolean;
}

interface Edge {
  from: Node;
  to: Node;
  qty: number;
  used: boolean;
}

const COL_WIDTH = 150;
const ROW_HEIGHT = 58;
const NODE_W = 118;
const NODE_H = 40;

/**
 * Resources are nodes; each process is a small node joining its inputs to its outputs (a
 * hyperedge drawn as a bipartite fan). Resources are layered by production depth so chains
 * read left to right. Processes used by the selected solution are highlighted with their
 * run counts.
 */
@Component({
  selector: 'app-process-graph',
  templateUrl: './process-graph.html',
  styleUrl: './process-graph.scss',
})
export class ProcessGraph {
  readonly instance = input.required<Instance>();
  readonly solution = input<Solution | null>(null);

  protected readonly nodeW = NODE_W;
  protected readonly nodeH = NODE_H;

  protected readonly runCounts = computed(() => {
    const counts = new Map<string, number>();
    for (const r of this.solution()?.runs ?? [])
      counts.set(r.process_id, (counts.get(r.process_id) ?? 0) + 1);
    return counts;
  });

  protected readonly layout = computed(() => {
    const inst = this.instance();
    const counts = this.runCounts();
    const highlight = this.solution() !== null;
    const colors = processColors(inst.processes.map((p) => p.id));

    // Resource level = 1 + max level of the inputs of any process producing it (bounded so
    // cycles terminate). Never-produced resources are level 0.
    const level = new Map(inst.resources.map((r) => [r.id, 0]));
    for (const _pass of inst.resources) {
      let changed = false;
      for (const p of inst.processes) {
        const inLevel = Math.max(-1, ...Object.keys(p.inputs).map((r) => level.get(r) ?? 0));
        for (const out of Object.keys(p.outputs)) {
          const want = Math.min(inst.resources.length, inLevel + 1);
          if ((level.get(out) ?? 0) < want) {
            level.set(out, want);
            changed = true;
          }
        }
      }
      if (!changed) break;
    }

    // Columns: resources at even columns 2*level, processes at odd columns between.
    const columns = new Map<number, Node[]>();
    const place = (col: number, node: Omit<Node, 'x' | 'y'>): Node => {
      const list = columns.get(col) ?? [];
      const n: Node = { ...node, x: col, y: list.length };
      list.push(n);
      columns.set(col, list);
      return n;
    };

    const resourceNodes = new Map<string, Node>();
    for (const r of inst.resources) {
      const start = inst.start_inventory[r.id] ?? 0;
      resourceNodes.set(
        r.id,
        place(2 * (level.get(r.id) ?? 0), {
          key: `r:${r.id}`,
          kind: 'resource',
          id: r.id,
          label: r.name || r.id,
          sub: `$${r.sell_price}${start ? ` · start ${start}` : ''}`,
          color: '#1f2f63',
          used: true,
        }),
      );
    }
    const edges: Edge[] = [];
    for (const p of inst.processes) {
      const inLevels = Object.keys(p.inputs).map((r) => level.get(r) ?? 0);
      const outLevels = Object.keys(p.outputs).map((r) => level.get(r) ?? 0);
      const col = inLevels.length
        ? 2 * Math.max(...inLevels) + 1
        : Math.max(1, 2 * Math.min(...(outLevels.length ? outLevels : [1])) - 1);
      const runs = counts.get(p.id) ?? 0;
      const used = !highlight || runs > 0;
      const node = place(col, {
        key: `p:${p.id}`,
        kind: 'process',
        id: p.id,
        label: p.name || p.id,
        sub: `${p.duration} tick${p.duration === 1 ? '' : 's'}${runs ? ` · ×${runs}` : ''}`,
        color: colors.get(p.id) ?? '#475569',
        used,
      });
      for (const [r, q] of Object.entries(p.inputs)) {
        const from = resourceNodes.get(r);
        if (from) edges.push({ from, to: node, qty: q, used });
      }
      for (const [r, q] of Object.entries(p.outputs)) {
        const to = resourceNodes.get(r);
        if (to) edges.push({ from: node, to, qty: q, used });
      }
    }

    // Pixel positions: center each column vertically.
    const tallest = Math.max(1, ...[...columns.values()].map((c) => c.length));
    const maxCol = Math.max(0, ...columns.keys());
    for (const [, list] of columns) {
      const offset = ((tallest - list.length) * ROW_HEIGHT) / 2;
      for (const n of list) {
        n.x = 10 + n.x * COL_WIDTH;
        n.y = 10 + offset + n.y * ROW_HEIGHT;
      }
    }
    const nodes = [...columns.values()].flat();
    return {
      nodes,
      edges,
      width: 20 + maxCol * COL_WIDTH + NODE_W,
      height: 20 + tallest * ROW_HEIGHT,
    };
  });

  protected readonly summary = computed(() => {
    const inst = this.instance();
    const used = [...this.runCounts().entries()].map(([id, n]) => `${id} ×${n}`).join(', ');
    return (
      `Process graph with ${inst.resources.length} resources and ${inst.processes.length} processes.` +
      (used ? ` Used in the selected solution: ${used}.` : '')
    );
  });

  protected path(e: Edge): string {
    const x1 = e.from.x + NODE_W;
    const y1 = e.from.y + NODE_H / 2;
    const x2 = e.to.x;
    const y2 = e.to.y + NODE_H / 2;
    if (x2 <= x1) {
      // Backward edge (cycle): loop below.
      const dy = Math.max(y1, y2) + NODE_H;
      return `M ${x1} ${y1} C ${x1 + 40} ${dy}, ${x2 - 40} ${dy}, ${x2} ${y2}`;
    }
    const mid = (x1 + x2) / 2;
    return `M ${x1} ${y1} C ${mid} ${y1}, ${mid} ${y2}, ${x2} ${y2}`;
  }

  protected labelPos(e: Edge): { x: number; y: number } {
    return { x: (e.from.x + NODE_W + e.to.x) / 2, y: (e.from.y + e.to.y) / 2 + NODE_H / 2 - 3 };
  }
}
