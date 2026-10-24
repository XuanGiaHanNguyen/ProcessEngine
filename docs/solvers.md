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

## Brute force does not assume "decide only at events"

An exchange argument suggests that some optimal schedule starts every run at tick 0 or at
a completion tick (shifting the earliest off-event start back to the previous event never
breaks inventory or worker limits, since neither changes between events). Faster exact
solvers may rely on that. Brute force deliberately does not: it also tries every in-between
tick, so if the argument were wrong the "exact solver == brute force" tests would catch it.
Declining to start a legal run now (to keep its inputs or worker for a later, better run)
is of course explored by every exact solver; the greedy solvers never do it, which is one
reason they are heuristics.

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
