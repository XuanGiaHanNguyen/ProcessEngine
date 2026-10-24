import { Action, Instance, ScheduledRun } from '../api/models';

/** A manual run as exported/imported: the instance plus the player's actions. */
export interface RunFile {
  format: 'processforge-run';
  version: 1;
  instance: Instance;
  actions: Action[];
}

export function makeRunFile(instance: Instance, history: Action[]): RunFile {
  return {
    format: 'processforge-run',
    version: 1,
    instance,
    actions: history.filter((a) => a.type !== 'finish'),
  };
}

export function parseRunFile(text: string): RunFile {
  const data = JSON.parse(text) as Partial<RunFile>;
  if (data.format !== 'processforge-run' || !data.instance || !Array.isArray(data.actions)) {
    throw new Error('Not a ProcessForge run file (expected format "processforge-run").');
  }
  return data as RunFile;
}

/**
 * Reconstructs the start tick of each run from the action history. This is bookkeeping, not
 * rules: the server already accepted every action, and the timeline endpoint re-validates.
 */
export function scheduleFromHistory(history: Action[], horizon: number): ScheduledRun[] {
  const runs: ScheduledRun[] = [];
  let tick = 0;
  for (const a of history) {
    if (a.type === 'start_run') runs.push({ process_id: a.process_id, start_tick: tick });
    else if (a.type === 'wait') tick += 1;
    else if (a.type === 'advance_to') tick = a.tick;
    else tick = horizon;
  }
  return runs;
}

// Share links carry the actions compactly: "s:<id>" start, "w" wait, "a:<tick>" advance.
export function encodeActions(actions: Action[]): string {
  const parts = actions.flatMap((a) =>
    a.type === 'start_run'
      ? [`s:${a.process_id}`]
      : a.type === 'wait'
        ? ['w']
        : a.type === 'advance_to'
          ? [`a:${a.tick}`]
          : [],
  );
  return parts.map(encodeURIComponent).join(',');
}

export function decodeActions(text: string): Action[] {
  if (!text) return [];
  return text.split(',').map((raw) => {
    const part = decodeURIComponent(raw);
    if (part === 'w') return { type: 'wait' } as const;
    if (part.startsWith('s:')) return { type: 'start_run', process_id: part.slice(2) } as const;
    if (part.startsWith('a:')) return { type: 'advance_to', tick: Number(part.slice(2)) } as const;
    throw new Error(`Unknown action "${part}" in link.`);
  });
}
