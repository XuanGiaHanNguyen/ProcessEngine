# ProcessForge HTTP API (v1)

This file is the single source of truth for the API used by the server (`server/`) and the
Angular models (`web/src/app/api/`). Base URL: `http://localhost:8080`.

## Conventions

- Request and response bodies are JSON (`Content-Type: application/json`).
- **Envelope fields** are camelCase (`jobId`, `sessionId`, `legalActions`, `timeLimitMs`).
  **Engine objects** (instances, schedules, solutions, session state) keep the engine's
  snake_case field names (`sell_price`, `proven_optimal`, `runtime_ms`). Requests accept
  `processId` or `process_id` in actions.
- Every error has the same shape and a 4xx/5xx status:
  ```json
  {"error": "insufficient_inventory", "reason": "not enough iron (need 3, have 1)"}
  ```
  Some errors add fields (`errors[]` for validation, or the unchanged session view for an
  illegal action).
- Limits: request bodies above 2 MiB → `413 payload_too_large`; `timeLimitMs` is clamped to
  30 000 ms (server flag `--max-time-limit`); at most 16 solvers per request; at most 1000
  live sessions (`503 session_limit`); sessions expire after 1 hour without use.
- CORS: every response, including preflight, carries `Access-Control-Allow-Origin`
  (`*` by default, `--cors-origin` to restrict).

## Schemas

### Instance

```json
{
  "name": "Sawmill",
  "resources": [{"id": "wood", "name": "Wood", "sell_price": 1}],
  "processes": [
    {"id": "saw", "name": "Saw planks", "inputs": {"wood": 2}, "outputs": {"plank": 1},
     "duration": 2, "max_runs": 5}
  ],
  "start_inventory": {"wood": 4},
  "workers": 1,
  "horizon": 4,
  "start_money": 0
}
```

Required: `resources`, `processes`, `workers` (>= 1), `horizon` (>= 0); each process needs
`id` and `duration` (>= 1). `name` defaults to `id`; `sell_price`, `start_money` default to 0;
`max_runs` is optional (unlimited). Unknown fields are rejected, except the metadata fields
`description`, `comment`, `reference_solutions`. Limits: quantities in recipes <= 1e6, prices
<= 1e6, horizon <= 1e6, workers <= 1e4.

### Schedule and ScheduledRun

```json
[{"process_id": "saw", "start_tick": 0}, {"process_id": "saw", "start_tick": 2}]
```
(An object `{"runs": [...]}` is also accepted.)

### Solution

```json
{
  "runs": [ScheduledRun],
  "earnings": 8,               // always computed by the validator
  "proven_optimal": true,
  "runtime_ms": 0.12,
  "solver_name": "branch_and_bound",
  "stats": {"nodes": 12, "pruned": 3},   // solver-specific integer counters
  "valid": true,
  "error": null                // string when the solver produced no valid schedule
}
```

### Action

```json
{"type": "start_run", "process_id": "saw"}
{"type": "wait"}
{"type": "advance_to", "tick": 5}
{"type": "finish"}             // appears in history only; use POST .../finish
```

### SessionState

```json
{
  "tick": 2, "horizon": 6,
  "inventory": {"ore": 0, "iron": 2},
  "money": 0,
  "projected_earnings": 13,    // what finishing now would score (running runs complete)
  "runs_in_progress": [{"process_id": "forge", "start": 2, "end": 5, "remaining": 3}],
  "free_workers": 1, "workers": 2,
  "run_counts": {"smelt": 2, "forge": 1},
  "history": [Action],
  "redo_available": 0,
  "finished": false
}
```

### SessionView (returned by every session endpoint)

```json
{
  "sessionId": "9f2c...",
  "state": SessionState,
  "legalActions": [Action],
  "processStatus": [
    {"processId": "forge", "name": "Forge steel", "legal": false,
     "code": "insufficient_inventory", "reason": "not enough Iron (need 2, have 0)"}
  ],
  "instance": Instance,        // GET, create and load only
  "redo": [Action]             // GET, create and load only; next action to redo first
}
```

`processStatus` gives the reason for every disabled Start button without sending an illegal
action. Rejection codes: `unknown_process`, `past_horizon`, `insufficient_inventory`,
`no_free_worker`, `max_runs_exceeded`, `at_horizon`, `invalid_tick`, `game_finished`,
`nothing_to_undo`, `nothing_to_redo`.

### Timeline (for the results view)

```json
{
  "earnings": 13,
  "runs": [{"run_index": 0, "process_id": "smelt", "start": 0, "end": 2, "slot": 0}],
  "points": [{"tick": 0, "inventory": {"ore": 2}, "busy_workers": 2, "value": 0}]
}
```
`slot` assigns each run to a worker lane (lowest free slot first). `points` hold the
inventory right after each tick where something starts or ends (plus 0 and T); `value` is
start money plus inventory at sell prices, not counting runs in progress. Computed by the
engine, so the frontend never re-implements the rules.

### ProgressEvent (WebSocket messages)

```json
{"seq": 3, "solver": "beam_search", "event": "improved",
 "elapsedMs": 41.7, "bestEarnings": 607, "nodes": 1200}
```
`event` is `improved` (new best-so-far), `heartbeat`, `solver_done`, `solver_failed`, or
`done` (whole job finished; `solver` is empty). `seq` starts at 0 and has no gaps.

## Endpoints

### Meta and instances

| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/api/health` | | `{"status": "ok"}` |
| GET | `/api/solvers` | | `[{name, description, exact, options: [{name, description, default}]}]` |
| GET | `/api/instances/samples` | | `[{id, name, description, resources, processes, workers, horizon}]` |
| GET | `/api/instances/samples/{id}` | | `Instance` (404 `sample_not_found`) |
| POST | `/api/instances/validate` | `Instance` | `{valid, errors[]}` (always 200) |
| POST | `/api/instances/generate` | `GeneratorParams` | `Instance` |
| POST | `/api/schedules/validate` | `{instance, schedule}` | `{valid, earnings, final_inventory, violation}` |
| POST | `/api/schedules/timeline` | `{instance, schedule}` | `Timeline` (422 if the schedule is invalid) |

`GeneratorParams`: `{"size": "small"|"medium"|"large", "seed", "resources", "processes",
"chain_depth", "workers", "horizon", "min_price", "max_price", "max_qty", "max_duration"}`,
all optional; `size` picks a preset and the other fields override it.

### Solving

| Method | Path | Body | Response |
|---|---|---|---|
| POST | `/api/solve` | `{instance, solvers: [name], options?}` | `202 {jobId}` |
| GET | `/api/solve/{jobId}` | | `Job` |
| DELETE | `/api/solve/{jobId}` | | `Job` (cancel requested) |
| WS | `/ws/solve/{jobId}` | | stream of `ProgressEvent` |

`options`: `{"timeLimitMs": 5000, "seed": 1, "params": {"beam_width": 32}}` (unknown keys
rejected). `params` are solver-specific; `GET /api/solvers` lists them with defaults.

`Job`:
```json
{
  "jobId": "a1b2...", "status": "queued" | "running" | "done" | "cancelled",
  "timeLimitMs": 5000,         // effective (clamped) limit
  "events": 17,                // events recorded so far
  "results": [{"solver": "greedy", "status": "queued"|"running"|"done"|"cancelled",
               "solution": Solution | null}]
}
```
Each solver runs as its own task on the server's thread pool. Cancelling stops running
solvers at their next check (anytime solvers still return their best schedule, with
`proven_optimal: false`) and skips queued ones. The WebSocket first replays every event so
far, then streams new ones, so connecting late (even after `done`) loses nothing.
`improved` events are throttled to one per 20 ms per solver; the final value always arrives
in `solver_done`.

Errors: `400 invalid_json | invalid_request | invalid_instance | invalid_solvers |
unknown_solver | invalid_options`, `404 job_not_found`, `413 payload_too_large`.

### Play mode

| Method | Path | Body | Response |
|---|---|---|---|
| POST | `/api/sessions` | `{instance}` or `{instanceId}` | `201 SessionView` |
| GET | `/api/sessions/{id}` | | `SessionView` (with instance, redo) |
| DELETE | `/api/sessions/{id}` | | `{deleted: true}` |
| POST | `/api/sessions/{id}/actions` | `Action` | `SessionView`, or `409 {error, reason, ...SessionView}` |
| POST | `/api/sessions/{id}/undo` | | `SessionView` or `409 nothing_to_undo` |
| POST | `/api/sessions/{id}/redo` | | `SessionView` or `409 nothing_to_redo` |
| POST | `/api/sessions/{id}/restart` | | `SessionView` |
| POST | `/api/sessions/{id}/finish` | | `SessionView + {solution, validated}` |
| POST | `/api/sessions/{id}/hint` | `{solver?, timeLimitMs?}` | `{suggestedAction, projectedEarnings, optimal, solver}` |
| POST | `/api/sessions/{id}/compare` | `{solvers, options?}` | `202 {jobId, playerEarnings, playerFinished}` |
| POST | `/api/sessions/{id}/load` | `{schedule}` | `SessionView` (schedule queued on the redo list) |

- The server never trusts client state: the client sends only actions, and the server
  applies them through the same rule code as the validator. An illegal action returns 409
  with the reason and the **unchanged** state.
- `finish` advances to the horizon, sells everything, and returns the manual run as a
  `Solution` (`solver_name: "manual"`); `validated` confirms the validator re-scored the
  exported schedule to the same earnings.
- `hint` solvers: `branch_and_bound` (default; exact search of the rest of the game within
  `timeLimitMs`, default 250, max 2000), `greedy`, `greedy_potential`. `optimal` says whether
  the projected earnings are proven best from the current state.
- `compare` runs solvers on the session's original instance (a normal job: poll it or open
  its WebSocket); `playerEarnings` is the player's current projected score.
- Replay: `load` resets the session and queues the schedule's actions on the redo list;
  `redo` then steps through it one action at a time.
- Errors: `400 invalid_json | invalid_action | invalid_schedule | unknown_solver`,
  `404 session_not_found | sample_not_found` (expired sessions are not found),
  `422` when a loaded schedule violates the rules, `503 session_limit`.

### Factory mode

Levels, designs and the per-hour report are defined in
[factory-spec.md](factory-spec.md); their fields stay snake_case. Every body names a bundled
level by id.

| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/api/factory/levels` | | `[{id, name, description}]` |
| GET | `/api/factory/levels/{id}` | | the level file as-is (with layout, objectives, reference designs); 404 `level_not_found` |
| GET | `/api/factory/solvers` | | `[{name, description}]` |
| POST | `/api/factory/validate` | `{level, design}` | `{valid, errors: [{code, message}], warnings: [{code, message}], build}` (always 200) |
| POST | `/api/factory/simulate` | `{level, design}` | `{result, warnings}`; an invalid design is 422 `invalid_design` with `errors` |
| POST | `/api/factory/solve` | `{level, solvers, timeLimitMs?, seed?}` | `{bound, boundSolved, results: [{solver_name, profit, runtime_ms, valid, error, stats, design}]}` |
| POST | `/api/factory/hint` | `{level, design}` | `{found: true, description, profit, gain, design}` or `{found: false}`; an invalid design is 422 `invalid_design` |

`result` (from `simulate`):

```json
{
  "profit": 810000, "sales": 8100000, "purchases": 5040000, "build": 2250000,
  "sold": {"stuffed-animal": 18},
  "hours": [
    {"hour": 6, "revenue": 900000, "purchases": 210000, "profit": -2820000,
     "sold": {"stuffed-animal": 2},
     "machines": [{"slot": "s1", "status": "busy", "progress": 1, "total": 1,
                   "missing": [], "inputs": {"cotton": 3, "string": 2},
                   "outputs": {"fabric": 2}, "batches_done": 5}],
     "belts": [5, 3, 2, 0, 0, 0, 0],
     "stock": {"cotton-supply": 0, "string-supply": 4},
     "delivered": {"cotton-supply": 8, "string-supply": 4}}
  ]
}
```

`hours[].belts[i]` is the number of units design belt `i` carried that hour; `machines` follow
the design's machine order. `delivered` is what each supplier delivered and charged that hour:
its order, or less when the level's warehouse (`storage`) was full. `solve` runs the solvers in parallel on the request (each limited
to `timeLimitMs`, default 2000, capped at 10 000) and returns each design re-scored by the
simulator, plus the flow LP upper bound `bound`.
