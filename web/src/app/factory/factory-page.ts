import { ChangeDetectionStrategy, Component, OnInit, computed, inject } from '@angular/core';

import * as ops from './design-ops';
import { CanvasSelection, FactoryCanvas } from './factory-canvas';
import { FactoryInspector } from './factory-inspector';
import { money } from './factory-models';
import { FactoryStore } from './factory-store';

/** Factory mode: edit a toy factory on a D3 floor, check it, and test it. */
@Component({
  selector: 'app-factory-page',
  imports: [FactoryCanvas, FactoryInspector],
  providers: [FactoryStore],
  templateUrl: './factory-page.html',
  styleUrl: './factory-page.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
  host: { '(window:keydown)': 'onKey($event)' },
})
export class FactoryPage implements OnInit {
  protected readonly store = inject(FactoryStore);
  protected readonly money = money;

  protected readonly target = computed(
    () =>
      this.store.level()?.reference_designs.find((r) => r.name === 'Best known')?.profit ?? null,
  );
  protected readonly budgetLeft = computed(() => {
    const budget = this.store.level()?.budget;
    return budget != null ? budget - this.store.buildCost() : null;
  });

  ngOnInit(): void {
    void this.store.init();
  }

  protected step(delta: number): void {
    const next = this.store.levels()[this.store.levelIndex() + delta];
    if (next) void this.store.open(next.id);
  }

  protected selectNode(id: string | null): void {
    this.store.selection.set(id ? { kind: 'node', id } : null);
  }

  protected selectBelt(index: number): void {
    this.store.selection.set({ kind: 'belt', index });
  }

  protected asCanvasSelection(): CanvasSelection {
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
