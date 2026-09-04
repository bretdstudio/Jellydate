import { describe, expect, it, vi } from 'vitest';
import sharp from 'sharp';
import { PosterService } from '../src/images/poster.js';

describe('PosterService', () => {
  it('creates and caches a correctly sized packed one-bit poster', async () => {
    const source = vi.fn(async () => sharp({
      create: {
        width: 200,
        height: 300,
        channels: 3,
        background: { r: 190, g: 80, b: 45 },
      },
    }).png().toBuffer());
    const service = new PosterService();
    const options = { width: 96, height: 144, fit: 'cover' as const };

    const first = await service.convert('movie:tag', source, options);
    const second = await service.convert('movie:tag', source, options);

    expect(first).toHaveLength(96 / 8 * 144);
    expect(second).toBe(first);
    expect(source).toHaveBeenCalledTimes(1);
  });
});
