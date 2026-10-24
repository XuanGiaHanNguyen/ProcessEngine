# ProcessForge: Production Process Optimizer

A C++ optimization engine with an Angular frontend. Given a set of production processes (recipes that turn input resources into output resources over time), a starting inventory, limited worker slots, and a time horizon, find the schedule that maximizes earnings. Multiple solver algorithms are pluggable and compared side by side in the UI.

> **For agents:** Read this whole file before starting. Work milestone by milestone, in order. Do not skip ahead. Each milestone has acceptance criteria; a milestone is done only when all of them pass. Ask for clarification instead of guessing when the spec is ambiguous.

---

## 1. Goals

1. Show strong data structures, algorithms, OOP, and system design in one repo.
2. Be demoable: someone can open the UI, load an instance, run solvers, and watch the results.
3. Be rigorous: every solver's output is checked by an independent validator, and heuristics are compared against exact solvers on small instances.
4. Support two ways of using it: **Auto mode** (pick solvers, let them run) and **Play mode** (the user makes the decisions themselves, tick by tick, then can compare their result against the solvers).

**Non-goals:** user accounts, databases, multiplayer, production-grade auth, mobile support.

---

## 2. Problem Model (v1, fixed unless the owner changes it)

Time is **discrete**: integer ticks `0 .. T`.

**Entities**
- `Resource`: id, name, `sell_price` (integer >= 0; 0 means it cannot be sold).
- `Process`: id, name, `inputs` (map resource -> qty), `outputs` (map resource -> qty), `duration` (integer ticks >= 1), optional `max_runs` (cap on total runs).
- `Instance`: list of resources, list of processes, `start_inventory` (map resource -> qty), `workers` (number of processes that can run in parallel, W >= 1), `horizon` T, optional `start_money`.

**Rules**
1. Starting a process run at tick `t` consumes its inputs from inventory immediately. The run occupies one worker slot during ticks `[t, t + duration)`.
2. Its outputs are added to inventory at tick `t + duration`.
3. At most `W` runs are in progress at any tick.
4. A run must finish at or before `T` (`t + duration <= T`) to count. Unfinished runs are invalid.
5. Inventory may never go negative.
6. At tick `T`, all remaining inventory is sold at `sell_price`. Earnings = `start_money + sum(qty * sell_price)` of final inventory.
7. Objective: **maximize final earnings.**

**Schedule format:** list of `{process_id, start_tick}` entries (one per run).

**Later variants (milestone 10, optional):** selling mid-game at fluctuating prices, deadlines/orders, setup times, resource capacity limits. Keep the model code extensible for these, but do not implement them early.

---

## 2.5 Play Mode (manual) Spec

Play mode lets a human run the same instance by hand. It must use **exactly the same rules as section 2**, enforced by the same code, so a manual result and a solver result are directly comparable.

**Engine: `GameSession`** (a stateful simulator, separate from the one-shot validator, but built on the same rule code so they cannot drift apart)
- `GameSession(const Instance&)` starts at tick 0 with the starting inventory.
- `state()` returns: current tick, inventory, money, runs in progress (with ticks remaining), free worker slots, history of actions.
- `legalActions()` returns every action allowed right now: `StartRun(process_id)` for each process whose inputs are available and that has a free worker slot and can finish by `T`, plus `Wait` (advance one tick) and `AdvanceTo(tick)` (skip ahead to the next event).
- `apply(action)` performs the action, or returns a clear error such as "not enough iron (need 3, have 1)" or "no free worker". Illegal actions never change state.
- `undo()` / `redo()` over the action history.
- `finish()` advances to tick `T`, sells all remaining inventory, and returns the final `Solution` (checked by the validator).
- `exportSchedule()` returns the manual run as a normal `Schedule` so it can be replayed, shared, or loaded into the timeline view.
- `hint()` (optional) asks a chosen solver for the best next action from the current state.

**Required properties**
- Deterministic: the same action list always gives the same result.
- Replaying the exported schedule through the validator gives the same earnings as the session reports. This is a required automated test.
- Session state is serializable to JSON so the server can persist it in memory and the UI can restore it.

**Play-mode features (UI, see milestone 8):**
- Step through ticks manually, start processes by clicking them, see inventory and workers update live.
- Undo/redo, restart, and "fast-forward to next event".
- Live "projected earnings if I stop now" and "best known solver result" for reference.
- **Challenge mode:** play first (solver results hidden), then press "Reveal" to see how your score compares to greedy, exact, and heuristic solvers, with a score as a percentage of the best known solution.
- **Assist mode:** a "Hint" button shows the next move a solver would make, and an optional "Show what the solver would do from here" overlay.
- **Replay:** load any solver's schedule into the same play screen and step through it tick by tick to see why it works.
- Share link or JSON export for a manual run.

---

## 3. Tech Stack

| Layer | Choice |
|---|---|
| Engine | C++20, CMake, no web code inside the engine library |
| Tests | GoogleTest (unit), a custom property/fuzz harness |
| Benchmarks | Google Benchmark plus a custom solver-comparison runner |
| JSON | nlohmann/json |
| Server | Crow or Drogon (HTTP + WebSocket), a thin wrapper around the engine |
| Frontend | Angular (latest stable), TypeScript strict mode, RxJS |
| Visualization | Cytoscape.js or D3 (graph), a custom or library Gantt timeline, Chart.js or ngx-charts |
| CI | GitHub Actions: build, test, lint on every push |
| Packaging | Docker (multi-stage) plus docker-compose for server + frontend |

Tooling: `clang-format`, `clang-tidy`, sanitizers (ASan/UBSan) in CI, ESLint + Prettier for Angular.

---

## 4. Repository Layout

```
processforge/
  PROJECT_PLAN.md
  README.md
  CMakeLists.txt
  engine/
    include/pf/        # public headers
      model.hpp        # Resource, Process, Instance, Schedule
      validator.hpp
      solver.hpp       # abstract Solver interface
      solvers/         # one header per solver
      generator.hpp    # random instance generator
      json_io.hpp
    src/
    tests/
    bench/
  server/
    src/               # HTTP/WebSocket wrapper
    tests/
  web/                 # Angular app
  instances/           # hand-made JSON instances (small, medium, large)
  docs/
    architecture.md
    solvers.md
    api.md
    images/
  docker/
  .github/workflows/
```

---

## 5. Core Interfaces

```cpp
struct Solution {
  std::vector<ScheduledRun> runs;   // {process_id, start_tick}
  int64_t earnings;                 // computed by validator, not trusted from solver
  bool proven_optimal;
  double runtime_ms;
  std::string solver_name;
};

class Solver {
 public:
  virtual ~Solver() = default;
  virtual std::string name() const = 0;
  virtual Solution solve(const Instance&, const SolveOptions&, ProgressSink*) = 0;
};
```

- `SolveOptions`: time limit, random seed, thread count.
- `ProgressSink`: callback receiving best-so-far solutions and stats during a solve (used for live streaming to the UI). Must be safe to leave null.
- Solvers register themselves in a `SolverRegistry` (factory pattern) so the server can list and construct them by name.
- The **validator is independent** of every solver. It replays a schedule tick by tick and reports the first rule violation or the final earnings. All solver outputs pass through it before being returned.

---

## 6. Milestones

### Milestone 0: Project setup
**Tasks**
- Create the repo layout, CMake build, clang-format/clang-tidy configs, GoogleTest integration.
- Add the GitHub Actions workflow (build + test + sanitizers).

**Acceptance**
- `cmake -B build && cmake --build build && ctest --test-dir build` passes on a clean checkout.
- CI is green with a placeholder test.

### Milestone 1: Model, JSON I/O, validator
**Tasks**
- Implement the model types from section 2.
- JSON serialization/deserialization with schema validation and helpful errors.
- Implement the validator (tick-by-tick simulation enforcing every rule).
- Write 5+ hand-made instances in `instances/`, including at least one with a known optimal answer calculated by hand (documented in a comment).

**Acceptance**
- Unit tests cover every rule violation (negative inventory, worker overflow, run past horizon, unknown process id, exceeded `max_runs`).
- The validator returns the hand-computed earnings for the hand-made instances.

### Milestone 2: Baseline solvers (greedy + brute force)
**Tasks**
- `GreedySolver`: repeatedly start the available process with the best value-per-tick ratio (document the exact ratio definition). Also implement one alternative greedy variant.
- `BruteForceSolver`: exhaustive search for tiny instances (for testing only; guard with a size limit).
- Solver registry.

**Acceptance**
- Both solvers produce validator-approved schedules on every hand-made instance.
- On tiny instances, brute force is >= greedy on every test. Brute force results are used as the ground truth in later tests.

### Milestone 2.5: Interactive session engine (Play mode core)
**Tasks**
- Implement `GameSession` as described in section 2.5. Share the rule logic (consuming inputs, worker slots, completion events, horizon check) with the validator through a common internal module so the two cannot disagree.
- Implement `legalActions()`, `apply()`, `undo()/redo()`, `finish()`, `exportSchedule()`, and JSON (de)serialization of session state.
- Add an optional `hint()` that calls a solver on the remaining problem from the current state (can be stubbed until milestone 4 and wired to the exact solver later).

**Acceptance**
- Every illegal action returns a specific error and leaves state unchanged (tested for each rule).
- Property test: for 1000 random instances, play a random sequence of legal actions to the end, export the schedule, run it through the validator, and confirm the earnings are identical.
- Undo/redo test: any sequence of apply/undo returns to the exact earlier state.
- A solver's schedule loaded into a session and stepped through gives the same final earnings the solver reported.

### Milestone 3: Instance generator and test harness
**Tasks**
- Seeded random generator with controls for number of resources, processes, chain depth, workers, horizon, and price ranges. Guarantee at least one profitable path.
- Property tests: every solver output passes the validator; exact solvers match brute force on small instances; solver results are deterministic for a fixed seed.
- Comparison runner CLI: runs a list of solvers over N generated instances and outputs a CSV (profit, runtime, optimality gap).

**Acceptance**
- 1000 random small instances run in CI in under 2 minutes with no validator failures.
- CLI prints a summary table.

### Milestone 4: Exact solvers
**Tasks**
- `DPSolver`: dynamic programming over a compressed state (inventory vector + tick + worker occupancy). Document the state definition and its complexity. Add a state-count limit and fail cleanly when exceeded.
- `BranchAndBoundSolver`: DFS with an admissible upper bound (document the bound; for example, a relaxation that ignores worker limits), best-first ordering, and pruning counters.
- Both report `proven_optimal` only when the search actually completed.

**Acceptance**
- Both match brute force on all small generated instances.
- Branch and bound reports nodes explored and nodes pruned.
- `docs/solvers.md` explains each algorithm, its state/bound, and its complexity.

### Milestone 5: Heuristic and relaxation solvers
**Tasks**
- `BeamSearchSolver` (configurable beam width).
- `SimulatedAnnealingSolver` (neighborhood moves: insert/remove/shift a run; repair step via the validator rules; cooling schedule configurable).
- `LPRelaxationSolver` (or min-cost-flow based bound): use an LP/relaxation to compute an upper bound and optionally guide a rounding heuristic. Pick a small dependency (for example GLPK or HiGHS) or implement a simple simplex yourself if the owner prefers zero dependencies. Ask the owner before adding a heavy dependency.
- Time-limit and seed support in every solver.

**Acceptance**
- On small instances, report optimality gap versus the exact solvers.
- On medium instances (where exact solvers time out), heuristics return valid solutions within the time limit.
- The LP bound is >= any solver's solution on every test instance.

### Milestone 6: Performance pass
**Tasks**
- Profile with `perf` or Instruments and fix the top hotspots.
- Use compact state representations, hash-based memoization with a good hash, and arena or pool allocation in the search solvers.
- Add Google Benchmark suites and record before/after numbers in `docs/solvers.md`.

**Acceptance**
- A documented speedup (target: at least 3x on DP or B&B) with before/after numbers.
- No sanitizer errors.

### Milestone 7: Server
**Tasks**
- REST endpoints (see section 7) plus a WebSocket for live progress.
- Run each solve on a worker thread pool; support cancel; enforce a max time limit and a max request size.
- CORS config for local development; structured logging; graceful shutdown.

**Acceptance**
- Integration tests: submit an instance, receive a validated solution; cancel a long solve; invalid input returns a clear 4xx JSON error.
- Play-mode integration tests: create a session, apply legal actions, get a 4xx with a reason for an illegal action (state unchanged), undo/redo, finish, and confirm the finished score equals the validator's score for the exported schedule.
- Sessions expire after inactivity and the session cap is enforced.
- Streaming shows at least two intermediate best-so-far updates on a medium instance.

### Milestone 8: Angular frontend
**Tasks**
1. App shell, routing, API service, typed models that mirror the API schema.
2. **Instance editor:** forms to add/edit resources, processes, workers, horizon, start inventory; JSON import/export; load sample instances; generate a random instance.
3. **Process graph view:** resources as nodes, processes as edges or hyperedges; highlight the processes used in the chosen solution.
4. **Solver panel:** pick one or more solvers, set time limit and seed, run, cancel.
5. **Results view:** Gantt timeline of runs per worker slot, inventory-over-time chart, earnings summary.
6. **Comparison view:** table and bar chart of earnings, runtime, optimality gap, and `proven_optimal` across solvers; live convergence chart (best-so-far vs. time) fed by the WebSocket.
7. **Play screen (manual mode):** a mode switch at the top (Auto / Play). In Play mode show: the current tick and a timeline scrubber; inventory and money panels; a list of processes with a Start button that is enabled only when the action is legal (with a tooltip explaining why it is disabled, for example "needs 2 more wood"); worker slots with progress bars for runs in progress; Wait and "Skip to next event" buttons; Undo/Redo/Restart; a live "projected earnings" readout.
8. **Challenge and assist:** a "Finish" button that ends the game and shows the final score; a "Reveal solvers" step that runs the chosen solvers and shows the player's score as a percentage of the best result with a ranked leaderboard; a Hint button; and a "Replay solver schedule" option that loads any solver's result into the play screen to step through.
9. **Shareable runs:** export and import a manual run as JSON, and optionally a share link that encodes the instance id and the action list.
10. Loading, error, and empty states everywhere; basic responsive layout; keyboard shortcuts in Play mode (for example space = wait, Z = undo).

**Acceptance**
- A user can go from "load sample" to "compare 3 solvers" in under 30 seconds with no console errors.
- A user can open Play mode on a sample instance, make moves by hand, undo a move, finish the game, and see their score compared against solvers, all without touching Auto mode.
- Disabled actions in Play mode always show the reason; the UI never lets an illegal action reach the server (and if one does, the error is shown cleanly).
- The end-to-end test covers one full manual playthrough.
- Component tests for the editor and the results view; one end-to-end test (Playwright or Cypress) covering the main flow.
- Lighthouse accessibility score >= 90.

### Milestone 9: Packaging and docs
**Tasks**
- Dockerfiles and docker-compose: `docker compose up` starts everything.
- `docs/architecture.md` with a diagram (engine, server, frontend, data flow, including the Play-mode session flow), `docs/api.md`, `docs/solvers.md`.
- Add a Play-mode GIF to the README (a short manual game followed by the solver reveal).
- README: what it is, a GIF demo, quick start, architecture diagram, solver comparison chart, benchmark table, how to run tests.
- Deploy a demo if possible (for example a small VM or Fly.io) and link it in the README.

**Acceptance**
- A fresh clone plus `docker compose up` gives a working app.
- README includes at least one GIF, one chart, and one benchmark table.

### Milestone 10 (optional stretch)
Pick any:
- Model variants: price changes over time, orders with deadlines, setup times, storage capacity.
- Parallel solvers (portfolio of solvers racing; parallel beam search).
- WebAssembly build of the engine so the demo runs without a server.
- Instance difficulty visualizer (how hard each instance is for each solver).

---

## 7. API Contract (v1)

```
GET    /api/solvers                  -> [{name, description, exact: bool, options: [...]}]
POST   /api/instances/validate       body: Instance           -> {valid, errors[]}
POST   /api/instances/generate       body: GeneratorParams    -> Instance
POST   /api/solve                    body: {instance, solvers[], options}
                                     -> {jobId}
GET    /api/solve/{jobId}            -> {status, results[]}
DELETE /api/solve/{jobId}            -> cancel
WS     /ws/solve/{jobId}             -> stream of {solver, elapsedMs, bestEarnings, nodes?, event}

# Play mode (manual)
POST   /api/sessions                 body: {instance | instanceId}   -> {sessionId, state, legalActions}
GET    /api/sessions/{id}            -> {state, legalActions, history}
POST   /api/sessions/{id}/actions    body: {type: "start_run"|"wait"|"advance_to", processId?, tick?}
                                     -> {state, legalActions} or 4xx {error, reason}
POST   /api/sessions/{id}/undo       -> {state, legalActions}
POST   /api/sessions/{id}/redo       -> {state, legalActions}
POST   /api/sessions/{id}/finish     -> {solution}   (earnings, schedule, validated)
POST   /api/sessions/{id}/hint       body: {solver?}  -> {suggestedAction, projectedEarnings}
POST   /api/sessions/{id}/compare    body: {solvers[], options}  -> {jobId}  (runs solvers on the original instance, reveals scores vs. the player)
POST   /api/sessions/{id}/load       body: {schedule}   -> {state}   (replay a solver or saved schedule step by step)
```

Sessions are held in memory with an expiry (for example 1 hour of inactivity) and a cap on how many exist at once. The server never trusts a client-supplied state; the client only sends actions.

Solution JSON includes: `runs[]`, `earnings`, `proven_optimal`, `runtime_ms`, `solver_name`, and `stats` (solver-specific counters). All schemas live in `docs/api.md` and are the single source of truth for both the server and the Angular models.

---

## 8. Testing Strategy

- **Unit tests:** model, JSON, validator (every rule), each solver on hand-made instances.
- **Property tests:** random instances; every solution passes the validator; exact solvers equal brute force; heuristics never beat the proven optimum; LP bound >= all results.
- **Determinism tests:** same seed gives identical results.
- **Server tests:** endpoints, cancellation, limits, malformed input.
- **Frontend tests:** component tests plus one end-to-end flow.
- **Sanitizers** (ASan/UBSan, and TSan for the server) in CI.
- **Performance regression:** record benchmark numbers; flag a >20% regression.

---

## 9. Engineering Rules for Agents

1. Work on one milestone at a time, in order. Keep each commit small with a clear message.
2. Never trust a solver's own reported earnings; always recompute with the validator.
3. No web or I/O code inside `engine/`.
4. Prefer clear code over clever code, but profile before optimizing and record the numbers.
5. Every public function or class gets a short doc comment; every non-obvious algorithm decision goes in `docs/solvers.md`.
6. Add tests with the code, not after.
7. Do not add heavy dependencies without asking the owner.
8. If the spec is ambiguous or a rule needs to change, stop and ask rather than silently changing the model.
9. Do not copy any company's assessment problem verbatim; this project uses its own generalized model.

---

## 10. Suggested Agent Split (if running several agents)

| Agent | Scope | Depends on |
|---|---|---|
| Engine agent | Milestones 0-6 including 2.5 (model, validator, `GameSession`, solvers, benchmarks) | none |
| Server agent | Milestone 7 (solve endpoints and session endpoints) | M1, M2.5, and at least M2 for real solvers |
| Frontend agent | Milestone 8 (Auto mode and Play mode) | API contract (section 7), can mock the server first |
| Docs/DevOps agent | Milestone 9, CI | all of the above |

Agree on the API contract and JSON schemas first so the server and frontend agents can work in parallel against mocks.

---

## 11. Definition of Done

- All milestones 0-9 acceptance criteria met.
- CI green, with sanitizers.
- `docker compose up` works from a clean clone.
- README has a demo GIF, an architecture diagram, a solver comparison chart, and a benchmark table.
- The owner can explain, for each solver, its algorithm, complexity, and when it wins or loses.
