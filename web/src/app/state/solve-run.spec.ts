import { of } from 'rxjs';

import { ApiService } from '../api/api.service';
import { Job, Solution } from '../api/models';
import { SolveRun } from './solve-run';

function solution(name: string, earnings: number, proven = false): Solution {
  return {
    runs: [],
    earnings,
    proven_optimal: proven,
    runtime_ms: 1,
    solver_name: name,
    stats: {},
    valid: true,
    error: null,
  };
}

describe('SolveRun', () => {
  it('computes gaps against the proven optimum and stops when the job is done', async () => {
    const job: Job = {
      jobId: 'j1',
      status: 'done',
      timeLimitMs: 1000,
      events: 3,
      results: [
        { solver: 'greedy', status: 'done', solution: solution('greedy', 80) },
        {
          solver: 'branch_and_bound',
          status: 'done',
          solution: solution('branch_and_bound', 100, true),
        },
        {
          solver: 'dp',
          status: 'done',
          solution: { ...solution('dp', 0), valid: false, error: 'state limit' },
        },
      ],
    };
    const api = {
      solve: vi.fn().mockResolvedValue({ jobId: 'j1' }),
      job: vi.fn().mockResolvedValue(job),
      stream: vi.fn().mockReturnValue(of()),
    } as unknown as ApiService;
    const run = new SolveRun(api);
    await run.start(
      {
        name: 'x',
        resources: [],
        processes: [],
        start_inventory: {},
        workers: 1,
        horizon: 1,
        start_money: 0,
      },
      ['greedy'],
      {},
    );
    await vi.waitFor(() => expect(run.busy()).toBe(false));
    expect(run.reference()).toEqual({ best: 100, proven: 100, value: 100 });
    const gaps = Object.fromEntries(run.rows().map((r) => [r.solver, r.gapPct]));
    expect(gaps).toEqual({ greedy: 20, branch_and_bound: 0, dp: null });
    run.stop();
  });

  it('reports API errors', async () => {
    const api = { solve: vi.fn().mockRejectedValue(new Error('boom')) } as unknown as ApiService;
    const run = new SolveRun(api);
    await run.start(
      {
        name: 'x',
        resources: [],
        processes: [],
        start_inventory: {},
        workers: 1,
        horizon: 1,
        start_money: 0,
      },
      ['greedy'],
      {},
    );
    expect(run.error()).toContain('boom');
    expect(run.busy()).toBe(false);
  });
});
