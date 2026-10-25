import { Objective, SimResult } from './factory-models';

/** Whether a simulated test meets a result-based objective (profit, sold, rate). */
export function testMeets(o: Objective, result: SimResult): boolean {
  switch (o.kind) {
    case 'profit':
      return result.profit >= (o.at_least ?? 0);
    case 'sold':
      return (result.sold[o.resource ?? ''] ?? 0) >= (o.at_least ?? 0);
    case 'rate': {
      const need = o.for_hours ?? 1;
      let run = 0;
      for (const h of result.hours) {
        run = (h.sold[o.resource ?? ''] ?? 0) >= (o.per_hour ?? 0) ? run + 1 : 0;
        if (run >= need) return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/**
 * Whether an objective is met, given the player's tests and how many distinct hours they
 * have stepped through. Each objective is an independent checkbox.
 */
export function objectiveMet(
  o: Objective,
  playerTests: SimResult[],
  hoursStepped: number,
): boolean {
  switch (o.kind) {
    case 'run_test':
      return playerTests.length > 0;
    case 'step_hours':
      return hoursStepped >= (o.hours ?? 1);
    default:
      return playerTests.some((r) => testMeets(o, r));
  }
}
