import { HourReport, SimResult } from './factory-models';
import { objectiveMet, testMeets } from './objectives';

function result(soldPerHour: number[], profit = 0): SimResult {
  const hours: HourReport[] = soldPerHour.map((n, hour) => ({
    hour,
    revenue: 0,
    purchases: 0,
    profit,
    sold: (n ? { toy: n } : {}) as Record<string, number>,
    machines: [],
    belts: [],
    stock: {},
  }));
  return {
    profit,
    sales: 0,
    purchases: 0,
    build: 0,
    sold: { toy: soldPerHour.reduce((a, b) => a + b, 0) },
    hours,
  };
}

describe('objectives', () => {
  it('checks profit and totals', () => {
    expect(testMeets({ kind: 'profit', at_least: 100, text: '' }, result([], 100))).toBe(true);
    expect(testMeets({ kind: 'profit', at_least: 100, text: '' }, result([], 99))).toBe(false);
    expect(
      testMeets({ kind: 'sold', resource: 'toy', at_least: 5, text: '' }, result([2, 3])),
    ).toBe(true);
  });

  it('needs the rate in consecutive hours', () => {
    const o = { kind: 'rate' as const, resource: 'toy', per_hour: 2, for_hours: 3, text: '' };
    expect(testMeets(o, result([2, 2, 0, 2, 2]))).toBe(false);
    expect(testMeets(o, result([0, 2, 3, 2]))).toBe(true);
  });

  it('counts player tests and stepped hours', () => {
    expect(objectiveMet({ kind: 'run_test', text: '' }, [], 0)).toBe(false);
    expect(objectiveMet({ kind: 'run_test', text: '' }, [result([])], 0)).toBe(true);
    expect(objectiveMet({ kind: 'step_hours', hours: 6, text: '' }, [], 5)).toBe(false);
    expect(objectiveMet({ kind: 'step_hours', hours: 6, text: '' }, [], 6)).toBe(true);
    expect(
      objectiveMet({ kind: 'profit', at_least: 1, text: '' }, [result([], 0), result([], 5)], 0),
    ).toBe(true);
  });
});
