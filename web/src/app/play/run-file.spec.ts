import { Action } from '../api/models';
import {
  decodeActions,
  encodeActions,
  makeRunFile,
  parseRunFile,
  scheduleFromHistory,
} from './run-file';

describe('run files', () => {
  const history: Action[] = [
    { type: 'start_run', process_id: 'smelt' },
    { type: 'wait' },
    { type: 'advance_to', tick: 4 },
    { type: 'start_run', process_id: 'forge' },
    { type: 'finish' },
  ];

  it('reconstructs start ticks from the history', () => {
    expect(scheduleFromHistory(history, 10)).toEqual([
      { process_id: 'smelt', start_tick: 0 },
      { process_id: 'forge', start_tick: 4 },
    ]);
  });

  it('round-trips actions through share-link encoding (finish is dropped)', () => {
    const encoded = encodeActions(history);
    expect(encoded).toBe('s%3Asmelt,w,a%3A4,s%3Aforge');
    expect(decodeActions(encoded)).toEqual(history.slice(0, 4));
    expect(decodeActions('')).toEqual([]);
    expect(() => decodeActions('x')).toThrow(/Unknown action/);
  });

  it('exports and re-imports a run file', () => {
    const instance = {
      name: 'X',
      resources: [],
      processes: [],
      start_inventory: {},
      workers: 1,
      horizon: 3,
      start_money: 0,
    };
    const file = makeRunFile(instance, history);
    expect(file.actions).toHaveLength(4);
    expect(parseRunFile(JSON.stringify(file))).toEqual(file);
    expect(() => parseRunFile('{"hello": 1}')).toThrow(/Not a ProcessForge run file/);
  });
});
