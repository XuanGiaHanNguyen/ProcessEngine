// Typed mirror of docs/api.md (the single source of truth for the API).
// Envelope fields are camelCase; engine objects keep the engine's snake_case names.

export interface Resource {
  id: string;
  name: string;
  sell_price: number;
}

/** Resource id -> quantity. */
export type Amounts = Record<string, number>;

export interface Process {
  id: string;
  name: string;
  inputs: Amounts;
  outputs: Amounts;
  duration: number;
  max_runs?: number | null;
}

export interface Instance {
  name: string;
  resources: Resource[];
  processes: Process[];
  start_inventory: Amounts;
  workers: number;
  horizon: number;
  start_money: number;
  description?: string;
}

export interface ScheduledRun {
  process_id: string;
  start_tick: number;
}

export interface Solution {
  runs: ScheduledRun[];
  earnings: number;
  proven_optimal: boolean;
  runtime_ms: number;
  solver_name: string;
  stats: Record<string, number>;
  valid: boolean;
  error: string | null;
}

export interface SolverOption {
  name: string;
  description: string;
  default: number;
}

export interface SolverInfo {
  name: string;
  description: string;
  exact: boolean;
  options: SolverOption[];
}

export interface SampleInfo {
  id: string;
  name: string;
  description: string;
  resources: number;
  processes: number;
  workers: number;
  horizon: number;
}

export interface GeneratorParams {
  size?: 'small' | 'medium' | 'large';
  seed?: number;
  resources?: number;
  processes?: number;
  chain_depth?: number;
  workers?: number;
  horizon?: number;
}

export interface ValidateResult {
  valid: boolean;
  errors: string[];
}

export interface SolveOptions {
  timeLimitMs?: number;
  seed?: number;
  params?: Record<string, number>;
}

export type JobStatus = 'queued' | 'running' | 'done' | 'cancelled';

export interface SolverRun {
  solver: string;
  status: JobStatus;
  solution: Solution | null;
}

export interface Job {
  jobId: string;
  status: JobStatus;
  timeLimitMs: number;
  events: number;
  results: SolverRun[];
}

export type ProgressEventKind = 'improved' | 'heartbeat' | 'solver_done' | 'solver_failed' | 'done';

export interface ProgressEvent {
  seq: number;
  solver: string;
  event: ProgressEventKind;
  elapsedMs: number;
  bestEarnings: number;
  nodes: number;
}

export interface TimelineRun {
  run_index: number;
  process_id: string;
  start: number;
  end: number;
  slot: number;
}

export interface TimelinePoint {
  tick: number;
  inventory: Amounts;
  busy_workers: number;
  value: number;
}

export interface Timeline {
  earnings: number;
  runs: TimelineRun[];
  points: TimelinePoint[];
}

// --- Play mode --------------------------------------------------------------------------------

export type Action =
  | { type: 'start_run'; process_id: string }
  | { type: 'wait' }
  | { type: 'advance_to'; tick: number }
  | { type: 'finish' };

export interface RunInProgress {
  process_id: string;
  start: number;
  end: number;
  remaining: number;
}

export interface SessionState {
  tick: number;
  horizon: number;
  inventory: Amounts;
  money: number;
  projected_earnings: number;
  runs_in_progress: RunInProgress[];
  free_workers: number;
  workers: number;
  run_counts: Record<string, number>;
  history: Action[];
  redo_available: number;
  finished: boolean;
}

export interface ProcessStatus {
  processId: string;
  name: string;
  legal: boolean;
  code: string | null;
  reason: string | null;
}

export interface SessionView {
  sessionId: string;
  state: SessionState;
  legalActions: Action[];
  processStatus: ProcessStatus[];
  instance?: Instance;
  redo?: Action[];
}

export interface FinishResult extends SessionView {
  solution: Solution;
  validated: boolean;
}

export interface Hint {
  suggestedAction: Action;
  projectedEarnings: number;
  optimal: boolean;
  solver: string;
}

export interface CompareResult {
  jobId: string;
  playerEarnings: number;
  playerFinished: boolean;
}

/** Error body shared by every 4xx/5xx response. */
export interface ApiErrorBody {
  error: string;
  reason: string;
  errors?: string[];
}
