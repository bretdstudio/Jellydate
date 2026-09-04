import { describe, expect, it, vi } from 'vitest';
import { retryDelayMs, withJellyfinLifecycleRetry } from '../src/jellyfin/lifecycle-retry.js';

function httpError(status: number, retryAfter?: string): Error & {
  response: { status: number; headers: Record<string, string> };
} {
  return Object.assign(new Error(`HTTP ${status}`), {
    response: {
      status,
      headers: retryAfter === undefined ? {} : { 'retry-after': retryAfter },
    },
  });
}

describe('Jellyfin lifecycle retry', () => {
  it('retries temporary 503 responses and honors Retry-After', async () => {
    const operation = vi.fn<() => Promise<string>>()
      .mockRejectedValueOnce(httpError(503, '1.5'))
      .mockResolvedValue('reported');
    const sleep = vi.fn<(milliseconds: number) => Promise<void>>().mockResolvedValue();

    await expect(withJellyfinLifecycleRetry(operation, { sleep })).resolves.toBe('reported');
    expect(operation).toHaveBeenCalledTimes(2);
    expect(sleep).toHaveBeenCalledWith(1_500);
  });

  it('stops after three attempts and does not retry permanent failures', async () => {
    const unavailable = httpError(503);
    const temporary = vi.fn<() => Promise<void>>().mockRejectedValue(unavailable);
    const sleep = vi.fn<(milliseconds: number) => Promise<void>>().mockResolvedValue();
    await expect(withJellyfinLifecycleRetry(temporary, { sleep })).rejects.toBe(unavailable);
    expect(temporary).toHaveBeenCalledTimes(3);
    expect(sleep).toHaveBeenNthCalledWith(1, 250);
    expect(sleep).toHaveBeenNthCalledWith(2, 500);

    const forbidden = vi.fn<() => Promise<void>>().mockRejectedValue(httpError(403));
    await expect(withJellyfinLifecycleRetry(forbidden, { sleep })).rejects.toThrow('HTTP 403');
    expect(forbidden).toHaveBeenCalledTimes(1);
  });

  it('parses HTTP-date Retry-After values and caps excessive waits', () => {
    const now = Date.parse('2026-09-04T12:00:00Z');
    expect(retryDelayMs(httpError(503, 'Thu, 04 Sep 2026 12:00:02 GMT'), 250, now))
      .toBe(2_000);
    expect(retryDelayMs(httpError(503, '60'), 250, now)).toBe(5_000);
  });
});
