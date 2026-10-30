/** Factory-mode level and design types, mirroring docs/factory-spec.md (snake_case kept). */

export type ResourceKind = 'raw' | 'part' | 'product';

export interface FactoryResource {
  id: string;
  name: string;
  kind: ResourceKind;
}

export interface Supplier {
  id: string;
  resource: string;
  max_rate: number;
  unit_cost: number;
  x: number;
  y: number;
}

export interface Recipe {
  id: string;
  name: string;
  inputs: Record<string, number>;
  outputs: Record<string, number>;
  hours: number;
}

export interface MachineType {
  id: string;
  name: string;
  cost: number;
  recipes: Recipe[];
}

export interface Listing {
  resource: string;
  price: number;
  demand: number;
  overflow_price: number;
}

export interface Slot {
  id: string;
  x: number;
  y: number;
}

export interface PlacedMachine {
  slot: string;
  type: string;
  recipe: string;
}

export interface Belt {
  from: string;
  to: string;
  resource: string;
  weight?: number;
}

export interface Design {
  orders: Record<string, number>;
  machines: PlacedMachine[];
  belts: Belt[];
}

export interface Objective {
  kind: 'profit' | 'sold' | 'rate' | 'run_test' | 'step_hours';
  text: string;
  at_least?: number;
  resource?: string;
  per_hour?: number;
  for_hours?: number;
  hours?: number;
}

export interface ReferenceDesign {
  name: string;
  initial?: boolean;
  design: Design;
  profit: number;
}

export interface FactoryLevel {
  format: string;
  id: string;
  name: string;
  description: string;
  hours: number;
  belt_capacity: number;
  buffer_batches: number;
  budget: number | null;
  resources: FactoryResource[];
  suppliers: Supplier[];
  machine_types: MachineType[];
  market: { x: number; y: number; listings: Listing[] };
  slots: Slot[];
  objectives: Objective[];
  reference_designs: ReferenceDesign[];
}

export interface FactoryLevelInfo {
  id: string;
  name: string;
  description: string;
}

/** Formats integer cents as dollars ("$16.20", "-$3.00"). */
export function money(cents: number): string {
  const sign = cents < 0 ? '-' : '';
  return `${sign}$${(Math.abs(cents) / 100).toFixed(2)}`;
}

export interface DesignIssue {
  code: string;
  message: string;
}

/** POST /api/factory/validate */
export interface DesignCheck {
  valid: boolean;
  errors: DesignIssue[];
  warnings: DesignIssue[];
  build: number;
}

export type MachineStatus = 'busy' | 'blocked' | 'starved';

export interface MachineHour {
  slot: string;
  status: MachineStatus;
  progress: number;
  total: number;
  missing: string[];
  inputs: Record<string, number>;
  outputs: Record<string, number>;
  batches_done: number;
}

export interface HourReport {
  hour: number;
  revenue: number;
  purchases: number;
  profit: number;
  sold: Record<string, number>;
  machines: MachineHour[];
  belts: number[];
  stock: Record<string, number>;
}

/** The per-hour report of POST /api/factory/simulate. */
export interface SimResult {
  profit: number;
  sales: number;
  purchases: number;
  build: number;
  sold: Record<string, number>;
  hours: HourReport[];
}

export interface FactorySolverResult {
  solver_name: string;
  profit: number;
  runtime_ms: number;
  valid: boolean;
  error: string | null;
  stats: Record<string, number>;
  design: Design;
}

/** POST /api/factory/solve */
export interface FactorySolveResponse {
  bound: number;
  boundSolved: boolean;
  results: FactorySolverResult[];
}
