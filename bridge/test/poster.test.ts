import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, describe, expect, it, vi } from 'vitest';
import sharp from 'sharp';
import { PosterService } from '../src/images/poster.js';

const directories: string[] = [];

afterEach(async () => {
  await Promise.all(directories.splice(0).map((directory) => rm(directory, {
    recursive: true,
    force: true,
  })));
});

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
    const service = new PosterService(null);
    const options = { width: 96, height: 144, fit: 'cover' as const };

    const first = await service.convert('movie:tag', source, options);
    const second = await service.convert('movie:tag', source, options);

    expect(first).toHaveLength(96 / 8 * 144);
    expect(second).toBe(first);
    expect(source).toHaveBeenCalledTimes(1);
  });

  it('reuses a persistent Playdate-ready poster after a Bridge restart', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'jellydate-artwork-'));
    directories.push(directory);
    const source = vi.fn(async () => sharp({
      create: {
        width: 384,
        height: 576,
        channels: 3,
        background: { r: 90, g: 150, b: 210 },
      },
    }).png().toBuffer());
    const options = { width: 96, height: 144, fit: 'cover' as const };

    const first = await new PosterService(directory).convert('movie:tag', source, options);
    const secondSource = vi.fn(async () => Buffer.alloc(0));
    const second = await new PosterService(directory).convert('movie:tag', secondSource, options);

    expect(second).toEqual(first);
    expect(source).toHaveBeenCalledTimes(1);
    expect(secondSource).not.toHaveBeenCalled();
  });

  it('deduplicates simultaneous requests for the same poster', async () => {
    const source = vi.fn(async () => sharp({
      create: {
        width: 384,
        height: 576,
        channels: 3,
        background: { r: 170, g: 110, b: 60 },
      },
    }).png().toBuffer());
    const service = new PosterService(null);
    const options = { width: 96, height: 144, fit: 'cover' as const };

    const [first, second] = await Promise.all([
      service.convert('movie:tag', source, options),
      service.convert('movie:tag', source, options),
    ]);

    expect(second).toBe(first);
    expect(source).toHaveBeenCalledTimes(1);
  });
});
