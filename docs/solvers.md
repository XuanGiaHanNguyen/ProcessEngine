# Solvers

Every solver implements `pf::Solver` (`engine/include/pf/solver.hpp`). A concrete solver
only implements `solveRaw()`, which returns a schedule plus counters. The public
`Solver::solve()` times the call and runs the schedule through the independent validator;
`Solution::earnings` always comes from the validator, never from the solver.

Solvers are listed and constructed by name through `SolverRegistry::builtin()`.

| Name | Exact | Milestone | Idea |
|---|---|---|---|
| `greedy` | no | 2 | best market-value gain per tick |
| `greedy_potential` | no | 2 | best time-aware potential gain per tick |
| `brute_force` | yes (tiny only) | 2 | exhaustive search, ground truth for tests |
| `dp` | yes | 4 | memoized DP over event states |
| `branch_and_bound` | yes (anytime) | 4 | DFS + dual upper bound + greedy incumbent |
| `beam_search` | no | 5 | beam over the event tree, rollout-scored |
| `simulated_annealing` | no | 5 | local search over schedules with rule-based repair |
| `lp_relaxation` | no (proves when tight) | 5 | time-indexed LP bound + LP-guided rounding |

Notation: `P` processes, `R` resources, `W` workers, horizon `T`, `E` = number of event
ticks visited (tick 0 plus every tick at which a run completes; `E <= T + 1`).

---

## Greedy (`greedy`, `greedy_potential`)

**Algorithm.** Simulate with `SimState`. At each event tick, repeatedly pick the legal
process with the highest score and start it, as long as the score is strictly positive.
When nothing more is worth starting, jump to the next completion tick. Stop when no run
is in progress and nothing is worth starting. Ties go to the lower process index, so the
result is deterministic.

**Ratio definitions.** For a process `p` with duration `d`, started at tick `t`:

- `greedy` (market value per tick):

  ```
  score(p) = ( sum_out q_o * price(o)  -  sum_in q_i * price(i) ) / d
  ```

  i.e. how much the inventory's sale value grows per worker-tick if the run completes.

- `greedy_potential` (potential value per tick): the same, but each output is valued at
  its *potential* `v_o(tau)` with `tau = T - t - d` ticks left after the run finishes.
  Inputs stay at market price (the alternative to running `p` is to sell them).

  ```
  v_r(tau) = max( price(r),
                  max over p consuming r with d_p <= tau of
                    ( sum_out q_o * v_o(tau - d_p) - sum_{other inputs j} q_j * price(j) ) / q_r )
  ```

  This lets the greedy see through intermediates with `sell_price = 0` (ore -> iron ->
  steel), and it is horizon-aware: an intermediate is worthless if the downstream chain
  can no longer finish. The table is computed bottom-up in `tau` and capped at
  `tau = 2048` (larger `tau` reuses the cap) to bound memory. It is a heuristic, not a
  bound: a resource used by several processes may be over-valued, and multi-input
  processes attribute the whole surplus to each input.

**Complexity.** `greedy`: `O(E * P * (I + O))` where `I`, `O` are inputs/outputs per process.
`greedy_potential` adds `O(min(T, 2048) * sum_p (I_p + O_p))` for the potential table.

**When it wins / loses.** Fast and good when processes are independent and profit is
visible at market prices. `greedy` fails on chains whose intermediates are worth 0;
`greedy_potential` fixes that but still commits inputs to the first good-looking use and
never waits for a better one (see the `BruteForce.FindsOptimumThatNeedsWaiting` test).

---

## Brute force (`brute_force`)

**Algorithm.** Depth-first search over every schedule. At the current tick it branches on
starting any legal process with index `>= from` (processes are taken in non-decreasing
index order so each multiset of same-tick starts is enumerated exactly once), plus one
"start nothing more now" branch that advances the clock:

- to `t + 1` if some process is legal right now (we might want it one tick later), or
- straight to the next completion tick if nothing is legal, because inventory and worker
  slots only change at completions, so nothing can become legal before then.

When no run is in progress and nothing is legal, the branch is a leaf: its value is the
inventory value at `T`. There is no pruning and no memoization, so it is obviously
correct, which is the point: it is the ground truth that the exact solvers (M4) are tested
against.

**Size guard.** Refuses instances with more than 8 processes or a horizon above 24, and
fails cleanly (no solution, `error` set) after `max_nodes` nodes (default 2e7). If the time
limit runs out it returns the best schedule found with `proven_optimal = false`.

**Complexity.** Exponential: up to `O((multisets of starts per tick)^(T))`, roughly
`O(C(P + W, W)^T)` nodes.

---

## Deciding only at events (exchange argument)

**Claim.** Some optimal schedule starts every run at tick 0 or at a tick where some run
completes ("event ticks").

**Proof sketch.** Take an optimal schedule and let `r` be the earliest run whose start `s`
is not an event tick; let `e < s` be the latest event tick before it. No run completes in
`(e, s]` and, since `r` is the earliest off-event start, no run starts in `(e, s)`. So the
inventory and the number of busy workers are the same at every tick of `[e, s)` as just
after the starts at `e`, and `r` was legal at `s`; hence it is legal at `e`. Moving `r` to
`e` only makes its outputs and its worker available earlier, which never invalidates a
later run, and leaves the final inventory (and so the earnings) unchanged. Each move
strictly decreases the sum of start ticks, so repeating it terminates with every start at
an event tick.

`dp` and `branch_and_bound` rely on this; `brute_force` deliberately does not (it also tries
every in-between tick). The property tests check `dp == branch_and_bound == brute_force`
on 1000 generated instances, so a flaw in the argument would show up there.

---

## Dynamic programming (`dp`)

**State.** Taken at tick 0 and at completion ticks, after completions are applied:
`(t, inventory[R], runs in progress as sorted (end, process) pairs, run counts of processes
that have max_runs)`. Run counts of uncapped processes and the start ticks of runs in
progress do not affect the future, so they are left out; this canonical key is what makes
different start orders collapse into one state.

**Recurrence.** `V(s) = max over multisets M of legal starts at t of V(next(s + M))`, where
`next` advances to the next completion; if nothing is running after `M`, the value is the
inventory value at `T`. Multisets are enumerated with process indices in non-decreasing
order, so each is generated exactly once. Memo entries store the value and the optimal
multiset, so the schedule is read back by following the stored decisions from tick 0.

**Complexity.** `O(S * M * P * R)` time and `O(S * (R + W))` memory, where `S` is the number
of distinct event states (bounded by `(T + 1) * prod_r (max inventory_r + 1) * (in-progress
configurations)`, exponential in `R` and `W`) and `M` is the number of start multisets per
state (`<= C(P + W, W)`).

**Limits.** Fails cleanly (no solution, `error` set) after `max_states` stored states
(default 2e6) or when the time limit runs out; it has no anytime answer. Reports `states`
and `memo_hits`.

**When it wins / loses.** Wins when many paths reach the same states (e.g. several
interchangeable processes). Loses on instances with large inventories or many resources,
where states rarely repeat: it cannot prune, so it must visit all of them.

---

## Branch and bound (`branch_and_bound`)

**Search tree.** The same event-based tree as `dp`: a node is `(state, from)`, where `from`
is the lowest process index that may still start at this tick. Children are "start `p >=
from`" and "start nothing more, advance to the next completion" (a leaf if nothing runs).

**Upper bound (admissible; `pf::DualBound`).** A potential (LP-dual) bound. Choose a price
`lambda >= 0` per free worker-tick and values `mu_r(tau)` per unit of resource `r` with
`tau` ticks left, such that

1. `mu_r(0) >= sell_price(r)` and `mu_r(tau)` is non-decreasing in `tau`;
2. for every process `p` with duration `d <= tau`:
   `sum_out q_o * mu_o(tau - d) <= sum_in q_i * mu_i(tau) + lambda * d`.

Define `Phi = start_money + sum_r inv_r * mu_r(T - t) + sum_{running} sum_out q_o * mu_o(T - end)
+ lambda * (free worker-ticks left)`. Starting a run cannot increase `Phi` (condition 2:
the inputs and `d` worker-ticks leave, the outputs arrive later); letting time pass cannot
increase it (condition 1, and free worker-ticks only shrink). At `T`, `Phi` equals the
earnings. So `Phi(state)` bounds every schedule from that state. It relaxes the worker
limit (only priced through `lambda`) and ignores `max_runs`.

`mu` is built bottom-up in `tau`: copy the row for `tau - 1`, then for each process whose
constraint is violated, spread the deficit over its inputs in proportion to their
quantities. Raising `mu(tau)` only strengthens the other constraints at the same `tau`
(it is their input side), so one pass per `tau` suffices. Processes with no inputs can only
be paid for by `lambda`; the smallest feasible `lambda` is found by bisection (the demand is
non-increasing in `lambda`). A few larger `lambda` values are also tabulated and the node
bound is the minimum over them. Cost: `O(K * T * sum_p |p|)` to build, `O(R + W * O)` per
node, `K <= 4`. Example: on `sawmill` the root bound is 8, exactly the optimum.

**Ordering and pruning.** Children are sorted by bound, best first (stable, so ties keep a
deterministic order). A node or child whose bound is `<=` the incumbent is pruned. The
incumbent starts as the better of the two greedy rollouts, so the search is anytime.

**Transpositions.** After a `(state, from)` subtree is searched to completion it is added to
a set; meeting it again is skipped. This is safe because the subtree's best value is then
already `<=` the incumbent, which only grows. Capped at `max_transpositions` (default 4e6).

**Stats.** `nodes`, `pruned`, `transposition_hits`, `root_bound`. `proven_optimal` is true
only if the search finished. Deterministic whenever it finishes; if the time limit cuts it
off, the answer depends on machine speed.

**When it wins / loses.** Usually far faster than `dp` (on 10 medium generated instances it
proved 9 optimal within 2 s, `dp` only 5), because the bound and a good incumbent prune most
of the tree. It loses when the bound is loose: many inputless processes or tight worker
limits (which the bound only prices) leave large gaps, and then it degrades to exhaustive
search.

---

## Beam search (`beam_search`)

**Algorithm.** Breadth-limited search over the same `(state, from)` tree as B&B, one action
per level (start one process, or advance to the next completion). Each child is scored by a
`greedy_potential` rollout from it; the rollout is itself a complete schedule, so the best
one seen is the answer (it is never worse than plain greedy). The `beam_width` best children
(default 16) survive; children with the same canonical key are merged.

**Complexity.** `O(L * B * P * rollout)` with `L` levels (about the number of runs plus
events), `B` the width, and a rollout costing `O(E * P)`.

**When it wins / loses.** The strongest heuristic on small and medium instances (it found
the optimum on all 200 small test instances). Rollout scoring can mislead it when a good
plan needs several "sacrifice" moves before the greedy rollout sees the payoff.

## Simulated annealing (`simulated_annealing`)

**Representation.** A list of `{process, start_tick}` runs. **Moves** (uniformly chosen):
insert a run (random process; half the time at an event tick of the current schedule),
remove a run, shift a run's start by -3..3 ticks, or change a run's process.
**Repair.** The candidate is replayed in start order through `SimState`; any run illegal at
that point (inputs, worker, horizon, max_runs) is dropped. So every candidate is valid and
scored exactly. **Acceptance** is Metropolis; **cooling** is geometric from
`initial_temperature` (0 = 5% of the starting earnings) to
`initial_temperature * final_temperature_ratio` (default 1e-3) over `iterations` steps
(default 20 000). It starts from the `greedy_potential` schedule and returns the best
schedule visited.

**Complexity.** `O(iterations * n * (log n + I + O))` for schedules of `n` runs.
Seeded by `SolveOptions::seed`; deterministic when the iteration budget ends the run.

**When it wins / loses.** Robust on medium/large instances where tree search cannot get
deep. Weak at long dependent chains: a single move rarely creates a whole profitable chain,
and repair drops downstream runs when an upstream one moves.

## LP relaxation (`lp_relaxation`)

**Model.** Variables `x[p][t] >= 0` for `t in [0, T - d_p]` (fractional runs). Maximize the
start value plus `sum net_market_value(p) * x[p][t]` subject to: inventory never negative
at any tick `< T` for every consumed resource (outputs count from `t + d_p`), at most `W`
runs in progress at each tick, and `sum_t x[p][t] <= max_runs`. Every valid schedule is an
integer point of this LP, so the LP optimum is an upper bound. Since `b >= 0` the slack
basis is feasible, and a small in-house dense primal simplex (Dantzig, falling back to
Bland after 50 degenerate pivots) solves it, with no new dependency.

**Rigorous bound.** The reported `lp_bound` is computed from the dual solution `y >= 0`:
`c.x <= y.b + W * sum_j max(0, c_j - (A^T y)_j)` (every `x_j <= W` by the worker rows), so it
is valid even if the simplex stopped early or rounding errors left `y` slightly infeasible.
It is then capped by the `DualBound` (both are valid, so the minimum is). LPs above 6e6
tableau entries are skipped and the `DualBound` alone is reported (`lp_too_large = 1`).

**Rounding.** Walk the ticks accumulating each process's LP mass; start a run when a
process has accumulated `>= 0.5` and the start is legal (largest mass first). A second
variant also fills idle workers greedily. The answer is the best of the two and
`greedy_potential`. `proven_optimal` is set when the schedule reaches `lp_bound`.

**Complexity.** Tableau `m x (n + m)` with `n ~ P*T` variables and `m ~ R_consumed*T + T + caps`
rows; each pivot is `O(m (n + m))`.

**When it wins / loses.** Gives the bound used to measure everyone's gap, and its rounding
is excellent when the LP is nearly integral (it was the best heuristic on 10 medium
instances, and even proved 3 of them optimal by meeting its own bound).
The LP cannot see integrality: on tiny instances with large indivisible recipes the
fractional plan rounds badly.

## Measured gaps (M5 acceptance)

200 small generated instances (seeds 0-199), gap vs the `branch_and_bound` optimum,
from `Heuristics.ReportGapAgainstExactOnSmallInstances`:

| solver | mean gap % | optimal |
|---|---|---|
| greedy | 6.94 | 145/200 |
| greedy_potential | 1.93 | 173/200 |
| beam_search | 0.00 | 200/200 |
| simulated_annealing (2000 it.) | 0.35 | 195/200 |
| lp_relaxation | 0.02 | 198/200 |

10 medium instances (`pf_compare --size medium --count 10 --seed 1 --time-limit 2000`), gap
vs the best known (the proven optimum where any solver proved one):

| solver | mean gap % | max gap % | best found | proven | mean ms |
|---|---|---|---|---|---|
| greedy_potential | 16.23 | 50.93 | 0 | 0 | 0.006 |
| beam_search | 3.59 | 23.89 | 7 | 0 | 3.3 |
| simulated_annealing | 8.36 | 25.63 | 2 | 0 | 15.2 |
| lp_relaxation | 1.37 | 5.65 | 6 | 3 | 3.6 |
| branch_and_bound | 0.35 | 3.53 | 9 | 9 | 352 |
| dp | 0.00 (5 solved) | | 5 | 5 | 1164 |

`dp` failed on the other 5 (state limit or time limit), which it reports as an error rather
than a schedule. `LpRelaxation.BoundIsAtLeastEverySolversEarnings` checks the LP bound
against every solver on all hand-made, 200 small and 15 medium instances.

## Play-mode hints

`GameSession::hint("branch_and_bound")` runs `BranchAndBoundSearch` from the current session
state (which may be mid-run, at any tick: the exchange argument holds from any starting
tick), with a short time limit (250 ms by default). The first run of the best continuation
becomes the hint (`StartRun` if it starts now, otherwise `AdvanceTo` its tick, or `Finish`
if nothing is worth starting); `optimal` says whether the search finished. Greedy hints
are also available.

---

## Instance generator (`pf::generate`)

Resources get a level `0..chain_depth`. Processes turn lower-level resources into
higher-level ones (1-2 inputs), so the process graph is acyclic; a few "source" processes
(probability `generator_probability`) make level-0 resources from nothing. Prices grow with
level, and intermediates are unsellable with probability `zero_price_probability`, which is
what makes the market-price greedy fail.

**Guaranteed profitable path.** Resources `r0..r_depth` form an explicit chain: step `L`
turns `q[L]` of `r_{L-1}` into `q[L+1]` of `r_L` (`q[depth+1] = 1`), so running each step once
consumes exactly what the previous step produced. The generator gives `r0` at least `q[1]`
units of start inventory, raises the horizon to fit the chain's total duration, and sets
`price(r_depth) > q[1] * price(r0)`. The chain schedule is returned as `witness`; the
generator tests check that it is valid and strictly beats selling the start inventory.

Presets: `small` (brute-force friendly: 3-4 resources, 2-4 processes, horizon 3-7,
1-2 workers), `medium` (8 resources, 10 processes, horizon 30, 3 workers), `large`
(20 resources, 30 processes, horizon 100, 5 workers).

## Comparison runner (`pf_compare`)

```
build/tools/pf_compare --solvers greedy,greedy_potential,brute_force --count 200 --size small
build/tools/pf_compare --instances instances --time-limit 2000
```

Writes one CSV row per (instance, solver): `earnings`, `profit` (earnings minus the value of
the untouched start inventory), `runtime_ms`, `proven_optimal`, and `gap_pct`, which is
relative to the proven optimum when any solver proved one, otherwise to the best earnings
found. Prints a per-solver summary table.

Baseline numbers (`pf_compare --count 200 --seed 1`, i.e. small seeds 1-200, Apple
M-series, Release):

| solver | mean gap % | max gap % | optimal hits | mean ms |
|---|---|---|---|---|
| greedy | 6.94 | 88.00 | 145/200 | 0.001 |
| greedy_potential | 1.93 | 50.00 | 173/200 | 0.001 |
| brute_force | 0.00 | 0.00 | 200/200 | 1.457 |

**Portable seeds.** Generated instances come from `pf::Random` (`pf/random.hpp`), not the
`std::` distributions: those are implementation-defined, so the same seed gave different
instances with libstdc++ and libc++. `Random.SameSequenceOnEveryPlatform` and
`Random.GeneratedInstancesArePinned` pin the sequence and an instance fingerprint, so a
seed now means the same instance on Linux CI and macOS. All numbers in this file were
measured after that change.

---

## Performance (M6)

Benchmarks: `engine/bench/solver_bench.cpp` (Google Benchmark). Instance `0` is
`instances/medium/factory.json`; `n > 0` is `GeneratorParams::medium(n)`. Apple M-series,
Release, single thread. Search statistics (nodes, pruned, states) are identical before and
after, so the speedups are pure implementation, not a smaller search.

```
cmake -B build-bench -DPF_BUILD_BENCHMARKS=ON -DPF_BUILD_TESTS=OFF
cmake --build build-bench -j && build-bench/engine/bench/pf_bench
```

| benchmark | work | before | after `canStart` | after compact state | speedup |
|---|---|---|---|---|---|
| `BM_DP/0` (factory) | 35 970 states | 257 ms | 143 ms | 39.2 ms | **6.6x** |
| `BM_DP/2` | 744 199 states | 3925 ms | 2758 ms | 566 ms | **6.9x** |
| `BM_DP/14` | 17 569 states | 63.5 ms | 30.1 ms | 11.0 ms | **5.8x** |
| `BM_DP/15` | 187 671 states | 1021 ms | 474 ms | 150 ms | **6.8x** |
| `BM_BranchAndBound/0` (factory) | 80 119 nodes | 86.3 ms | 55.0 ms | 12.8 ms | **6.7x** |
| `BM_BranchAndBound/3` | 575 302 nodes | 810 ms | 651 ms | 131 ms | **6.2x** |
| `BM_BranchAndBound/6` | 4.14 M nodes | 6529 ms | 5388 ms | 1148 ms | **5.7x** |
| `BM_BranchAndBound/9` | 146 823 nodes | 171 ms | 141 ms | 31.5 ms | **5.4x** |
| `BM_BranchAndBound/15` | 999 171 nodes | 1174 ms | 854 ms | 186 ms | **6.3x** |

"Before" and "after `canStart`" were measured by rebuilding the earlier commits (`9e4c727`,
`8fc1b04`) with the portable generator patched in, so all three columns run on exactly the
same instances (the identical state and node counts confirm it).

**What the profile showed** (macOS `sample` on `BM_BranchAndBound/8`, before): about 55% of
samples in `malloc`/`free`, then `unordered_set::find` (pointer chasing through
`std::vector<int64_t>` keys), then `SimState::checkStart` formatting error strings
(`std::to_string`, `string::append`) for processes that could not start.

**Fixes, in order:**

1. `SimState::canStart()` checks the rules without building a message; solvers use it in
   inner loops and `checkStart()` only formats a message for an actual violation.
2. A compact, fixed-width state (`src/solvers/compact_state.hpp`):
   `[tick | inventory | capped run counts | W sorted packed (end << 32 | process) slots | from]`.
   It is canonical, so the state array is its own hash key (no key building), and copying a
   state is one contiguous `memcpy` instead of three heap vectors.
3. Pool allocation: B&B and DP keep one scratch buffer per search depth, reused for the
   whole search, so after warm-up a node expansion does not allocate.
4. `FlatStateMap`: open addressing with linear probing; keys live in one contiguous arena,
   entries store the full 64-bit hash so mismatches rarely touch the key. This also removed
   a ~1.4 s overshoot of the time limit, which was the cost of freeing millions of
   `unordered_set` nodes.

The compact rules duplicate `SimState`'s semantics for speed. That is acceptable because
(a) every solver result is still re-checked by the independent validator and (b)
`CompactState.MatchesSimStateOnRandomPlay` drives both with the same random actions on 300
instances and requires identical inventory, legality, events, value, and encoding.

CI runs the benchmarks on every push and uploads `bench.json`. Comparing against a stored
baseline to flag >20% regressions needs a stable runner, so for now it is done by hand
(compare two `bench.json` files).

---

## Factory solvers (Factory mode)

Factory mode optimizes a *design* (machines, orders, belts; see
[factory-spec.md](factory-spec.md)), not a schedule. The solvers live in
`engine/include/pf/factory/solvers.hpp` and follow the same rule as the schedule solvers:
`FactorySolver::solve()` is non-virtual, checks the returned design, and takes its profit
from `simulate()`.

All three search over *machine lists* and *order rates*. A machine list becomes a design by
`autoWiredDesign`: machines go into slots upstream first, and every producer of a resource
is belted to every consumer of it (and to the market if it buys it). Each candidate is scored
by running the full 24-hour simulation (a few microseconds).

| Name | Idea |
|---|---|
| `greedy` | From an empty floor, repeatedly add the *chain* (a machine plus producers for any input nobody makes yet) that raises profit most, tuning orders by coordinate descent after each add; stop when nothing helps, then drop machines whose removal helps. Deterministic. |
| `local_search` | Simulated annealing from the greedy design: add a chain, remove a machine, swap a machine's recipe, or nudge one order; the temperature starts at the level's highest market price and falls linearly over the time limit; the best design gets its orders re-tuned and is pruned. |
| `flow_lp` | Solve the flow LP below, keep its whole machine counts plus the largest fractional ones, take its order rates, then run the greedy from there. |

**Upper bound (`flowUpperBound`).** A linear program over totals: batches `X_j` and machines
`n_j` per recipe, order rates `o_s`, and units sold at full price `F_p` and at the overflow
price `V_p`. Constraints: per resource, consumed + sold <= bought + produced; `X_j <= n_j *
floor((T - 1) / hours_j)` (nothing arrives before hour 1); `sum n_j <=` slots; build cost <=
budget; `o_s <= max_rate`; `F_p <= demand * (T - 1)`. Every simulated design satisfies these,
so the LP optimum bounds every design's profit. It ignores timing, belts and integrality, so
it is 2-3x the best designs found.

`pf_factory --time-limit 3000 instances/factory/*.json` (Apple M-series):

| Level | Initial | `greedy` | `local_search` (3 s) | `flow_lp` | Bound |
|---|---|---|---|---|---|
| Easy: First Stitch | $8,100 | $49,900 | $58,100 | $58,100 | $158,755 |
| Hard: Full Line | $106,700 | $155,700 | $171,100 | $142,450 | $397,845 |
| Expert: Toy Empire | $41,000 | $253,325 | $410,700 | $354,475 | $1,047,104 |

### Factory hints

`suggestChange` (`engine/src/factory/hint.cpp`, `POST /api/factory/hint`, the Play tab)
tries every single change to the player's draft and returns the one that raises profit the
most: one order up or down by 1 or 3, two orders by 1 together, a belt's split weight up or
down, removing a belt, a new belt between any producer and consumer of a resource, removing a
machine, and replacing a machine (another recipe or another type) or building one in an empty
slot, belted to every producer and consumer, also tried with its orders re-tuned by
coordinate descent. Every candidate is checked with `checkDesign` and scored with
`simulate`, so a hint is always valid and its gain exact; it runs in tens of milliseconds.
Following the hints from the initial design reaches the best known $58,100 on Easy; on Hard
they climb from $106,700 to $116,300 and then report that no single change helps, which is
where Auto's bigger rebuilds come in.
