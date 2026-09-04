const DEFAULT_RETRY_DELAY_MS = 250;
const MAX_RETRY_DELAY_MS = 5_000;
const DEFAULT_MAX_ATTEMPTS = 3;

interface RetryOptions {
  readonly maxAttempts?: number;
  readonly sleep?: (milliseconds: number) => Promise<void>;
  readonly now?: () => number;
}

interface ResponseLike {
  readonly status?: unknown;
  readonly headers?: unknown;
}

interface ErrorLike {
  readonly response?: ResponseLike;
}

export async function withJellyfinLifecycleRetry<T>(
  operation: () => Promise<T>,
  options: RetryOptions = {},
): Promise<T> {
  const maxAttempts = Math.max(1, Math.floor(options.maxAttempts ?? DEFAULT_MAX_ATTEMPTS));
  const sleep = options.sleep ?? sleepFor;
  const now = options.now ?? Date.now;
  let attempt = 0;

  while (true) {
    try {
      return await operation();
    } catch (error) {
      attempt += 1;
      if (attempt >= maxAttempts || responseStatus(error) !== 503) throw error;
      const fallbackMs = DEFAULT_RETRY_DELAY_MS * 2 ** (attempt - 1);
      await sleep(retryDelayMs(error, fallbackMs, now()));
    }
  }
}

export function retryDelayMs(error: unknown, fallbackMs: number, nowMs: number): number {
  const header = retryAfterHeader(error);
  let delayMs = fallbackMs;
  if (typeof header === 'number' && Number.isFinite(header)) {
    delayMs = header * 1_000;
  } else if (typeof header === 'string') {
    const seconds = Number(header.trim());
    if (Number.isFinite(seconds)) {
      delayMs = seconds * 1_000;
    } else {
      const dateMs = Date.parse(header);
      if (Number.isFinite(dateMs)) delayMs = dateMs - nowMs;
    }
  }
  return Math.min(MAX_RETRY_DELAY_MS, Math.max(0, Math.round(delayMs)));
}

function responseStatus(error: unknown): number | undefined {
  if (!error || typeof error !== 'object') return undefined;
  const status = (error as ErrorLike).response?.status;
  return typeof status === 'number' ? status : undefined;
}

function retryAfterHeader(error: unknown): unknown {
  if (!error || typeof error !== 'object') return undefined;
  const headers = (error as ErrorLike).response?.headers;
  if (!headers || typeof headers !== 'object') return undefined;
  if ('get' in headers && typeof headers.get === 'function') {
    return headers.get('retry-after');
  }
  const record = headers as Record<string, unknown>;
  return record['retry-after'] ?? record['Retry-After'];
}

function sleepFor(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
