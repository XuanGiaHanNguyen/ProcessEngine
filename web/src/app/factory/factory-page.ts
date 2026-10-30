import { NgTemplateOutlet } from '@angular/common';
import {
  ChangeDetectionStrategy,
  Component,
  OnInit,
  computed,
  inject,
  signal,
} from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import {
  lucideBot,
  lucideCheck,
  lucideChevronLeft,
  lucideChevronRight,
  lucideCircleAlert,
  lucideLightbulb,
  lucidePanelRightClose,
  lucidePanelRightOpen,
  lucidePause,
  lucidePencil,
  lucidePlay,
  lucideRedo2,
  lucideRotateCcw,
  lucideSkipBack,
  lucideUndo2,
  lucideWrench,
  lucideX,
} from '@ng-icons/lucide';

import * as ops from './design-ops';
import { FactoryAuto } from './factory-auto';
import { FactoryLine3d } from './factory-line-3d';
import { FloorSelection } from './floor-layout';
import { FactoryInspector } from './factory-inspector';
import { Design, money } from './factory-models';
import { MARKET_ICON, iconFor } from './icons';
import { FactoryPlay } from './factory-play';
import { MoneyChart } from './money-chart';
import { FactoryStore } from './factory-store';

type PanelId = 'build' | 'auto' | 'play';

/**
 * The toy factory: edit it on a 3D floor, test and play it back, let the optimizers build
 * it (Auto), and get hints, a score, a timeline and share links (Play).
 */
@Component({
  selector: 'app-factory-page',
  imports: [
    FactoryAuto,
    FactoryLine3d,
    FactoryInspector,
    FactoryPlay,
    MoneyChart,
    NgIcon,
    NgTemplateOutlet,
  ],
  viewProviders: [
    provideIcons({
      lucideBot,
      lucideCheck,
      lucideChevronLeft,
      lucideChevronRight,
      lucideCircleAlert,
      lucideLightbulb,
      lucidePanelRightClose,
      lucidePanelRightOpen,
      lucidePause,
      lucidePencil,
      lucidePlay,
      lucideRedo2,
      lucideRotateCcw,
      lucideSkipBack,
      lucideUndo2,
      lucideWrench,
      lucideX,
    }),
  ],
  providers: [FactoryStore],
  templateUrl: './factory-page.html',
  styleUrl: './factory-page.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
  host: { '(window:keydown)': 'onKey($event)' },
})
export class FactoryPage implements OnInit {
  protected readonly store = inject(FactoryStore);
  protected readonly money = money;
  protected readonly panels: { id: PanelId; label: string; icon: string }[] = [
    { id: 'build', label: 'Build', icon: 'lucideWrench' },
    { id: 'auto', label: 'Auto', icon: 'lucideBot' },
    { id: 'play', label: 'Play', icon: 'lucideLightbulb' },
  ];
  /** Which tab of the right panel is open. */
  protected readonly panel = signal<PanelId>('build');
  /** Whether the right panel is folded away to a strip of tab buttons. It starts folded, so
   * the floor gets the room; selecting something or a tab button unfolds it. */
  protected readonly collapsed = signal(true);

  protected readonly target = computed(
    () =>
      this.store.level()?.reference_designs.find((r) => r.name === 'Best known')?.profit ?? null,
  );
  protected readonly budgetLeft = computed(() => {
    const budget = this.store.level()?.budget;
    return budget != null ? budget - this.store.buildCost() : null;
  });

  /** The design on the floor: the test being played back, or the draft. */
  protected readonly shownDesign = computed<Design | null>(
    () => this.store.viewedTest()?.design ?? this.store.draft(),
  );

  /** Units of each product sold up to and including the current hour. */
  protected readonly soldSoFar = computed(() => {
    const t = this.store.viewedTest();
    if (!t) return [];
    const totals = new Map<string, number>();
    for (const h of t.result.hours.slice(0, this.store.hour() + 1)) {
      for (const [r, n] of Object.entries(h.sold)) totals.set(r, (totals.get(r) ?? 0) + n);
    }
    return [...totals].map(([id, qty]) => ({ id, qty, icon: iconFor(id) }));
  });

  /** During playback: what the selected node is doing this hour. */
  protected readonly inspectedHour = computed(() => {
    const sel = this.store.selection();
    const t = this.store.viewedTest();
    const h = this.store.currentHour();
    const level = this.store.level();
    if (sel?.kind !== 'node' || !t || !h || !level) return null;
    const name = (r: string) => level.resources.find((x) => x.id === r)?.name ?? r;
    if (sel.id === 'market') {
      return {
        icon: MARKET_ICON,
        title: 'Market',
        status: h.revenue > 0 ? 'busy' : 'starved',
        statusText: `Earned ${money(h.revenue)} this hour`,
        buffers: Object.entries(h.sold).map(([r, n]) => ({
          icon: iconFor(r),
          label: `${name(r)} sold`,
          value: n,
        })),
      };
    }
    const supplier = level.suppliers.find((x) => x.id === sel.id);
    if (supplier) {
      return {
        icon: iconFor(supplier.resource),
        title: `${name(supplier.resource)} supplier`,
        status: 'busy',
        statusText: `Delivers ${t.design.orders[supplier.id] ?? 0} per hour`,
        buffers: [{ icon: null, label: 'Waiting in stock', value: h.stock[supplier.id] ?? 0 }],
      };
    }
    const i = t.design.machines.findIndex((m) => m.slot === sel.id);
    const mh = h.machines[i];
    if (!mh) return null;
    const type = level.machine_types.find((x) => x.id === t.design.machines[i].type);
    const statusText =
      mh.status === 'busy'
        ? `Working: hour ${mh.progress} of ${mh.total}`
        : mh.status === 'blocked'
          ? 'Blocked: its output is full and nothing takes it away'
          : `Waiting for ${mh.missing.map(name).join(', ')}`;
    return {
      icon: null,
      title: `${type?.name ?? 'Machine'} ${sel.id}`,
      status: mh.status,
      statusText,
      buffers: [
        ...Object.entries(mh.inputs).map(([r, n]) => ({
          icon: iconFor(r),
          label: `${name(r)} in`,
          value: n,
        })),
        ...Object.entries(mh.outputs).map(([r, n]) => ({
          icon: iconFor(r),
          label: `${name(r)} out`,
          value: n,
        })),
        { icon: null, label: 'Batches done', value: mh.batches_done },
      ],
    };
  });

  ngOnInit(): void {
    // A share link (?level=…&design=…) opens that level with that design as the draft.
    const params = new URLSearchParams(location.search);
    const shared = { level: params.get('level'), design: params.get('design') };
    if (shared.level) history.replaceState(null, '', location.pathname);
    void this.store.init(shared);
  }

  /** Tab label for the i-th level: Easy and Hard, then Expert when there are three. */
  protected difficulty(i: number): string {
    const n = this.store.levels().length;
    if (n === 2 || n === 3) return ['Easy', 'Hard', 'Expert'][i];
    return `Level ${i + 1}`;
  }

  protected selectNode(id: string | null): void {
    this.store.selection.set(id ? { kind: 'node', id } : null);
    if (id) this.openPanel('build');
  }

  protected selectBelt(index: number): void {
    this.store.selection.set({ kind: 'belt', index });
    this.openPanel('build');
  }

  /** Opens a tab of the right panel, unfolding it if it was folded away. Selecting a node or
   * belt opens Build, where its inspector is. */
  protected openPanel(id: PanelId): void {
    this.panel.set(id);
    this.collapsed.set(false);
  }

  protected asFloorSelection(): FloorSelection {
    return this.store.selection();
  }

  protected resetDraft(): void {
    const initial = this.store.initialDesign();
    if (initial) this.store.replaceDraft(initial.design);
  }

  /** Undo/redo, Delete to remove the selected belt or machine, Escape to deselect. */
  protected onKey(e: KeyboardEvent): void {
    const target = e.target as HTMLElement | null;
    if (target && ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName)) return;
    if (this.store.viewedTest()) {
      if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
        this.store.stepBy(e.key === 'ArrowRight' ? 1 : -1);
        e.preventDefault();
      } else if (e.key === ' ' && target?.tagName !== 'BUTTON') {
        this.store.togglePlay();
        e.preventDefault();
      } else if (e.key === 'Escape') {
        this.store.selection.set(null);
      }
      return;
    }
    const mod = e.metaKey || e.ctrlKey;
    if (mod && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      if (e.shiftKey) this.store.redo();
      else this.store.undo();
    } else if (mod && e.key.toLowerCase() === 'y') {
      e.preventDefault();
      this.store.redo();
    } else if (e.key === 'Escape') {
      this.store.selection.set(null);
    } else if (e.key === 'Delete' || e.key === 'Backspace') {
      const sel = this.store.selection();
      if (sel?.kind === 'belt') {
        this.store.edit((_, d) => ops.removeBelt(d, sel.index));
        this.store.selection.set(null);
      } else if (
        sel?.kind === 'node' &&
        this.store.draft()?.machines.some((m) => m.slot === sel.id)
      ) {
        this.store.edit((_, d) => ops.removeMachine(d, sel.id));
      } else {
        return;
      }
      e.preventDefault();
    }
  }
}
