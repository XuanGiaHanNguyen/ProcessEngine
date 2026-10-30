import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { Observable, catchError, firstValueFrom, throwError } from 'rxjs';

import {
  Action,
  ApiErrorBody,
  CompareResult,
  FinishResult,
  GeneratorParams,
  Hint,
  Instance,
  Job,
  ProgressEvent,
  SampleInfo,
  ScheduledRun,
  SessionView,
  SolveOptions,
  SolverInfo,
  Timeline,
  ValidateResult,
} from './models';
import {
  Design,
  DesignCheck,
  FactoryLevel,
  FactoryLevelInfo,
  FactorySolveResponse,
  SimResult,
} from '../factory/factory-models';

/** An API failure with the server's machine code and human reason. */
export class ApiError extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
    readonly reason: string,
    readonly body?: unknown,
  ) {
    super(reason);
  }
}

/** Converts HttpErrorResponse into ApiError (network errors get code "network"). */
function toApiError(err: unknown): ApiError {
  if (err instanceof HttpErrorResponse) {
    const body = err.error as Partial<ApiErrorBody> | null;
    if (body && typeof body === 'object' && typeof body.error === 'string') {
      return new ApiError(err.status, body.error, body.reason ?? body.error, body);
    }
    if (err.status === 0) {
      return new ApiError(0, 'network', 'Cannot reach the ProcessForge server. Is it running?');
    }
    return new ApiError(err.status, 'http_error', `${err.status} ${err.statusText}`);
  }
  return new ApiError(0, 'unknown', String(err));
}

/**
 * Typed client for docs/api.md. Methods return Promises (one-shot calls); the solve stream
 * is an Observable. Paths are relative, so the dev-server proxy and the production nginx
 * both route them to the backend.
 */
@Injectable({ providedIn: 'root' })
export class ApiService {
  private readonly http = inject(HttpClient);

  private get<T>(path: string): Promise<T> {
    return firstValueFrom(
      this.http.get<T>(path).pipe(catchError((e) => throwError(() => toApiError(e)))),
    );
  }

  private post<T>(path: string, body: unknown = {}): Promise<T> {
    return firstValueFrom(
      this.http.post<T>(path, body).pipe(catchError((e) => throwError(() => toApiError(e)))),
    );
  }

  private delete<T>(path: string): Promise<T> {
    return firstValueFrom(
      this.http.delete<T>(path).pipe(catchError((e) => throwError(() => toApiError(e)))),
    );
  }

  health(): Promise<{ status: string }> {
    return this.get('/api/health');
  }
  solvers(): Promise<SolverInfo[]> {
    return this.get('/api/solvers');
  }
  samples(): Promise<SampleInfo[]> {
    return this.get('/api/instances/samples');
  }
  sample(id: string): Promise<Instance> {
    return this.get(`/api/instances/samples/${encodeURIComponent(id)}`);
  }
  factoryLevels(): Promise<FactoryLevelInfo[]> {
    return this.get('/api/factory/levels');
  }
  factoryLevel(id: string): Promise<FactoryLevel> {
    return this.get(`/api/factory/levels/${encodeURIComponent(id)}`);
  }
  validateDesign(level: string, design: Design): Promise<DesignCheck> {
    return this.post('/api/factory/validate', { level, design });
  }
  simulateDesign(
    level: string,
    design: Design,
  ): Promise<{ result: SimResult; warnings: DesignCheck['warnings'] }> {
    return this.post('/api/factory/simulate', { level, design });
  }
  solveFactory(
    level: string,
    solvers: string[],
    timeLimitMs: number,
  ): Promise<FactorySolveResponse> {
    return this.post('/api/factory/solve', { level, solvers, timeLimitMs });
  }
  validateInstance(instance: Instance): Promise<ValidateResult> {
    return this.post('/api/instances/validate', instance);
  }
  generate(params: GeneratorParams): Promise<Instance> {
    return this.post('/api/instances/generate', params);
  }
  timeline(instance: Instance, schedule: ScheduledRun[]): Promise<Timeline> {
    return this.post('/api/schedules/timeline', { instance, schedule });
  }

  solve(instance: Instance, solvers: string[], options: SolveOptions): Promise<{ jobId: string }> {
    return this.post('/api/solve', { instance, solvers, options });
  }
  job(id: string): Promise<Job> {
    return this.get(`/api/solve/${id}`);
  }
  cancel(id: string): Promise<Job> {
    return this.delete(`/api/solve/${id}`);
  }

  /** Live progress for a job. Completes after the "done" event or when the socket closes. */
  stream(jobId: string): Observable<ProgressEvent> {
    return new Observable<ProgressEvent>((subscriber) => {
      const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
      const socket = new WebSocket(`${protocol}//${location.host}/ws/solve/${jobId}`);
      socket.onmessage = (msg) => {
        const event = JSON.parse(String(msg.data)) as ProgressEvent;
        subscriber.next(event);
        if (event.event === 'done') {
          subscriber.complete();
          socket.close(1000);
        }
      };
      socket.onerror = () =>
        subscriber.error(new ApiError(0, 'websocket', 'Progress stream failed.'));
      socket.onclose = () => subscriber.complete();
      // Close with an explicit normal code: closing without one makes some servers echo the
      // reserved code 1005, which browsers report as a protocol error.
      return () => {
        socket.onmessage = null;
        socket.onerror = null;
        if (socket.readyState === WebSocket.OPEN) {
          socket.close(1000);
        } else if (socket.readyState === WebSocket.CONNECTING) {
          // Closing a socket that is still connecting logs a browser warning; close on open.
          socket.onopen = () => socket.close(1000);
        }
      };
    });
  }

  createSession(source: { instance: Instance } | { instanceId: string }): Promise<SessionView> {
    return this.post('/api/sessions', source);
  }
  session(id: string): Promise<SessionView> {
    return this.get(`/api/sessions/${id}`);
  }
  act(id: string, action: Action): Promise<SessionView> {
    return this.post(`/api/sessions/${id}/actions`, action);
  }
  undo(id: string): Promise<SessionView> {
    return this.post(`/api/sessions/${id}/undo`);
  }
  redo(id: string): Promise<SessionView> {
    return this.post(`/api/sessions/${id}/redo`);
  }
  restart(id: string): Promise<SessionView> {
    return this.post(`/api/sessions/${id}/restart`);
  }
  finish(id: string): Promise<FinishResult> {
    return this.post(`/api/sessions/${id}/finish`);
  }
  hint(id: string, solver = 'branch_and_bound'): Promise<Hint> {
    return this.post(`/api/sessions/${id}/hint`, { solver });
  }
  compare(id: string, solvers: string[], options: SolveOptions): Promise<CompareResult> {
    return this.post(`/api/sessions/${id}/compare`, { solvers, options });
  }
  load(id: string, schedule: ScheduledRun[]): Promise<SessionView> {
    return this.post(`/api/sessions/${id}/load`, { schedule });
  }
  deleteSession(id: string): Promise<{ deleted: boolean }> {
    return this.delete(`/api/sessions/${id}`);
  }
}
