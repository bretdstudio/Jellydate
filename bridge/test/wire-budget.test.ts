import { describe, expect, it } from 'vitest';
import { WireBudget } from '../src/streaming/wire-budget.js';

describe('WireBudget', () => {
  it('refills over time and caps burst capacity', () => {
    const budget = new WireBudget(1_000, 500, 0);
    expect(budget.trySpend(500, 0)).toBe(true);
    expect(budget.trySpend(1, 0)).toBe(false);
    expect(budget.trySpend(250, 250)).toBe(true);
    expect(budget.trySpend(500, 10_000)).toBe(true);
    expect(budget.trySpend(1, 10_000)).toBe(false);
  });

  it('allows a forced recovery keyframe without accumulating debt', () => {
    const budget = new WireBudget(1_000, 500, 0);
    expect(budget.trySpend(800, 0, true)).toBe(true);
    expect(budget.trySpend(1, 0)).toBe(false);
    expect(budget.trySpend(100, 100)).toBe(true);
  });
});
