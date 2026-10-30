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
proved 8 optimal within 3 s, `dp` only 3), because the bound and a good incumbent prune most
of the tree. It loses when the bound is loose: many inputless processes or tight worker
limits (which the bound only prices) leave large gaps, and then it degrades to exhaustive
search.

---

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

Baseline numbers (200 small instances, seed 1, Apple M-series, Release):

| solver | mean gap % | max gap % | optimal hits | mean ms |
|---|---|---|---|---|
| greedy | 8.21 | 86.96 | 152/200 | 0.002 |
| greedy_potential | 1.65 | 50.00 | 185/200 | 0.002 |
| brute_force | 0.00 | 0.00 | 200/200 | 1.197 |
