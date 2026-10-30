import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Injectable, inject } from '@angular/core';
import { catchError, firstValueFrom, throwError } from 'rxjs';

import {
  Design,
  DesignCheck,
  DesignHint,
  FactoryLevel,
  FactoryLevelInfo,
  FactorySolveResponse,
  FactorySolverInfo,
  SimResult,
} from '../factory/factory-models';

/** Error body of every non-2xx response (docs/api.md). */
export interface ApiErrorBody {
  error: string;
  reason: string;
  errors?: string[];
}

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
      return new ApiError(0, 'network', 'Cannot reach the ProcessEngine server. Is it running?');
    }
    return new ApiError(err.status, 'http_error', `${err.status} ${err.statusText}`);
  }
  return new ApiError(0, 'unknown', String(err));
}

/**
 * Typed client for the factory endpoints of docs/api.md. Methods return Promises. Paths are relative, so the dev-server proxy and the production nginx
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

  health(): Promise<{ status: string }> {
    return this.get('/api/health');
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
  factorySolvers(): Promise<FactorySolverInfo[]> {
    return this.get('/api/factory/solvers');
  }
  solveFactory(
    level: string,
    solvers: string[],
    timeLimitMs: number,
    seed?: number,
  ): Promise<FactorySolveResponse> {
    return this.post('/api/factory/solve', { level, solvers, timeLimitMs, seed });
  }
  /** The single change to `design` that raises profit the most. */
  factoryHint(level: string, design: Design): Promise<DesignHint> {
    return this.post('/api/factory/hint', { level, design });
  }
}
