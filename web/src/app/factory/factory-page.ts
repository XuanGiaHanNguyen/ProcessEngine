import {
  ChangeDetectionStrategy,
  Component,
  OnInit,
  computed,
  inject,
  signal,
} from '@angular/core';

import { ApiService } from '../api/api.service';
import { FactoryCanvas } from './factory-canvas';
import {
  Design,
  FactoryLevel,
  FactoryLevelInfo,
  MachineType,
  Recipe,
  ReferenceDesign,
  money,
} from './factory-models';
import { iconFor } from './icons';

/** What the inspector shows for the selected node. */
type Inspected =
  | { kind: 'supplier'; title: string; icon: string; rate: number; max: number; cost: string }
  | { kind: 'machine'; title: string; type: MachineType; recipe: Recipe }
  | { kind: 'slot'; title: string }
  | { kind: 'market'; title: string };

/** Factory mode: pick a level, look at the factory floor, inspect nodes, compare tests. */
@Component({
  selector: 'app-factory-page',
  imports: [FactoryCanvas],
  templateUrl: './factory-page.html',
  styleUrl: './factory-page.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class FactoryPage implements OnInit {
  private readonly api = inject(ApiService);

  protected readonly levels = signal<FactoryLevelInfo[]>([]);
  protected readonly level = signal<FactoryLevel | null>(null);
  protected readonly shown = signal<ReferenceDesign | null>(null);
  protected readonly selected = signal<string | null>(null);
  protected readonly error = signal<string | null>(null);

  protected readonly money = money;
  protected readonly iconFor = iconFor;

  protected readonly levelIndex = computed(() => {
    const id = this.level()?.id;
    return this.levels().findIndex((l) => l.id === id);
  });
  protected readonly tests = computed(() =>
    (this.level()?.reference_designs ?? []).filter((r) => r.initial),
  );
  protected readonly target = computed(
    () => this.level()?.reference_designs.find((r) => r.name === 'Best known')?.profit ?? null,
  );
  protected readonly design = computed<Design | null>(() => this.shown()?.design ?? null);
  protected readonly buildCost = computed(() => {
    const level = this.level();
    const design = this.design();
    if (!level || !design) return 0;
    return design.machines.reduce(
      (sum, m) => sum + (level.machine_types.find((t) => t.id === m.type)?.cost ?? 0),
      0,
    );
  });

  protected readonly inspected = computed<Inspected | null>(() => {
    const id = this.selected();
    const level = this.level();
    if (!id || !level) return null;
    if (id === 'market') return { kind: 'market', title: 'Market' };
    const supplier = level.suppliers.find((s) => s.id === id);
    if (supplier) {
      const res = level.resources.find((r) => r.id === supplier.resource);
      return {
        kind: 'supplier',
        title: `${res?.name ?? supplier.resource} supplier`,
        icon: iconFor(supplier.resource),
        rate: this.design()?.orders[id] ?? 0,
        max: supplier.max_rate,
        cost: money(supplier.unit_cost),
      };
    }
    const placed = this.design()?.machines.find((m) => m.slot === id);
    const type = placed && level.machine_types.find((t) => t.id === placed.type);
    const recipe = type?.recipes.find((r) => r.id === placed?.recipe);
    if (type && recipe) return { kind: 'machine', title: type.name, type, recipe };
    return { kind: 'slot', title: `Empty slot ${id}` };
  });

  async ngOnInit(): Promise<void> {
    try {
      const levels = await this.api.factoryLevels();
      this.levels.set(levels);
      if (levels.length) await this.open(levels[0].id);
    } catch (e) {
      this.error.set(e instanceof Error ? e.message : String(e));
    }
  }

  /** Loads a level and shows its initial design. */
  async open(id: string): Promise<void> {
    const level = await this.api.factoryLevel(id);
    this.level.set(level);
    this.selected.set(null);
    this.shown.set(level.reference_designs.find((r) => r.initial) ?? null);
  }

  protected step(delta: number): void {
    const next = this.levels()[this.levelIndex() + delta];
    if (next) void this.open(next.id);
  }

  protected resourceName(id: string): string {
    return this.level()?.resources.find((r) => r.id === id)?.name ?? id;
  }

  protected entries(rec: Record<string, number>): { id: string; qty: number }[] {
    return Object.entries(rec).map(([id, qty]) => ({ id, qty }));
  }
}
