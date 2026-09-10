import { describe, expect, it } from 'vitest';
import { PACKED_FRAME_BYTES, SCREEN_HEIGHT, SCREEN_WIDTH } from '../src/protocol/constants.js';
import { atkinsonDither, orderedDither8x8 } from '../src/transcoder/dither.js';

describe('orderedDither8x8', () => {
  it('packs a black frame into 12,000 zero bytes', () => {
    const packed = orderedDither8x8(new Uint8Array(SCREEN_WIDTH * SCREEN_HEIGHT));
    expect(packed).toHaveLength(PACKED_FRAME_BYTES);
    expect(packed.every((byte) => byte === 0)).toBe(true);
  });

  it('packs a white frame into 12,000 set bytes', () => {
    const gray = new Uint8Array(SCREEN_WIDTH * SCREEN_HEIGHT).fill(255);
    const packed = orderedDither8x8(gray);
    expect(packed.every((byte) => byte === 0xff)).toBe(true);
  });

  it('creates a stable spatial pattern for mid-gray', () => {
    const gray = new Uint8Array(SCREEN_WIDTH * SCREEN_HEIGHT).fill(128);
    const first = orderedDither8x8(gray);
    const second = orderedDither8x8(gray);
    expect(first).toEqual(second);
    const setBits = first.reduce((sum, byte) => sum + popcount(byte), 0);
    expect(setBits).toBe(SCREEN_WIDTH * SCREEN_HEIGHT / 2);
  });

  it('packs a 200x120 hardware frame into 3,000 bytes', () => {
    const gray = new Uint8Array(200 * 120).fill(255);
    const packed = orderedDither8x8(gray, 200, 120);
    expect(packed).toHaveLength(3_000);
    expect(packed.every((byte) => byte === 0xff)).toBe(true);
  });

  it('packs a 240x144 balanced hardware frame into 4,320 bytes', () => {
    const gray = new Uint8Array(240 * 144).fill(255);
    const packed = orderedDither8x8(gray, 240, 144);
    expect(packed).toHaveLength(4_320);
    expect(packed.every((byte) => byte === 0xff)).toBe(true);
  });

  it('uses conservative temporal hysteresis around a threshold', () => {
    const gray = new Uint8Array(SCREEN_WIDTH * SCREEN_HEIGHT).fill(0);
    gray[0] = 5;
    const previousBlack = Buffer.alloc(PACKED_FRAME_BYTES);
    const previousWhite = Buffer.alloc(PACKED_FRAME_BYTES);
    previousWhite[0] = 0x80;
    const staysBlack = orderedDither8x8(gray, SCREEN_WIDTH, SCREEN_HEIGHT, previousBlack, 8);
    const staysWhite = orderedDither8x8(gray, SCREEN_WIDTH, SCREEN_HEIGHT, previousWhite, 8);
    expect(staysBlack[0]! & 0x80).toBe(0);
    expect(staysWhite[0]! & 0x80).toBe(0x80);
  });
});

describe('atkinsonDither', () => {
  it('packs solid black and white artwork without introducing noise', () => {
    const black = atkinsonDither(new Uint8Array(96 * 144), 96, 144);
    const white = atkinsonDither(new Uint8Array(96 * 144).fill(255), 96, 144);
    expect(black).toHaveLength(96 / 8 * 144);
    expect(black.every((byte) => byte === 0)).toBe(true);
    expect(white.every((byte) => byte === 0xff)).toBe(true);
  });

  it('produces a deterministic, distributed pattern for static mid-gray artwork', () => {
    const gray = new Uint8Array(96 * 144).fill(128);
    const first = atkinsonDither(gray, 96, 144);
    const second = atkinsonDither(gray, 96, 144);
    expect(first).toEqual(second);
    const setBits = first.reduce((sum, byte) => sum + popcount(byte), 0);
    expect(setBits).toBeGreaterThan(96 * 144 * 0.35);
    expect(setBits).toBeLessThan(96 * 144 * 0.65);
  });
});

function popcount(value: number): number {
  let count = 0;
  for (let bits = value; bits !== 0; bits &= bits - 1) count += 1;
  return count;
}
