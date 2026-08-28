import { describe, expect, it } from 'vitest';
import { applyFrameDelta, encodeFrameDelta } from '../src/transcoder/delta.js';

describe('frame delta codec', () => {
  it('round-trips sparse changes and coalesces short unchanged gaps', () => {
    const previous = Buffer.alloc(12_000, 0x55);
    const next = Buffer.from(previous);
    next[100] = 0xaa;
    next[102] = 0xbb;
    next[8_000] = 0xcc;
    const delta = encodeFrameDelta(previous, next);
    const decoded = Buffer.from(previous);
    applyFrameDelta(decoded, delta);
    expect(decoded).toEqual(next);
    expect(delta.length).toBeLessThan(32);
  });

  it('uses an empty delta for an identical frame', () => {
    const frame = Buffer.alloc(3_000, 0xa5);
    expect(encodeFrameDelta(frame, frame)).toHaveLength(0);
  });

  it('compresses repeated replacement bytes', () => {
    const previous = Buffer.alloc(12_000, 0x00);
    const next = Buffer.from(previous);
    next.fill(0xff, 2_000, 3_000);
    const delta = encodeFrameDelta(previous, next);
    const decoded = Buffer.from(previous);
    applyFrameDelta(decoded, delta);
    expect(decoded).toEqual(next);
    expect(delta.length).toBe(5);
  });

  it('rejects malformed records', () => {
    expect(() => applyFrameDelta(Buffer.alloc(10), Buffer.from([0, 0, 0, 2, 1])))
      .toThrow('Invalid delta record');
  });
});
