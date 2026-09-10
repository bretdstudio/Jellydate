import { createHash, randomUUID } from 'node:crypto';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { mkdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import sharp from 'sharp';
import { atkinsonDither } from '../transcoder/dither.js';

const MAX_CACHE_ITEMS = 64;
const PIPELINE_VERSION = 'poster-v2-atkinson';

export interface PosterOptions {
  readonly width: number;
  readonly height: number;
  readonly fit?: 'contain' | 'cover';
}

export class PosterService {
  private readonly cache = new Map<string, Buffer>();
  private readonly pending = new Map<string, Promise<Buffer>>();

  constructor(
    private readonly cacheDirectory: string | null = join(
      process.env.XDG_CACHE_HOME?.trim() || join(homedir(), '.cache'),
      'jellydate',
      'artwork',
    ),
  ) {}

  async convert(
    cacheKey: string,
    source: () => Promise<Buffer>,
    options: PosterOptions = { width: 400, height: 240, fit: 'contain' },
  ): Promise<Buffer> {
    const fit = options.fit ?? 'cover';
    const sizedKey = `${PIPELINE_VERSION}:${cacheKey}:${options.width}x${options.height}:${fit}`;
    const cached = this.cache.get(sizedKey);
    if (cached) {
      this.touch(sizedKey, cached);
      return cached;
    }

    const pending = this.pending.get(sizedKey);
    if (pending) return pending;

    const conversion = this.loadOrConvert(sizedKey, source, options, fit);
    this.pending.set(sizedKey, conversion);
    try {
      return await conversion;
    } finally {
      this.pending.delete(sizedKey);
    }
  }

  private async loadOrConvert(
    sizedKey: string,
    source: () => Promise<Buffer>,
    options: PosterOptions,
    fit: 'contain' | 'cover',
  ): Promise<Buffer> {
    const expectedBytes = options.width / 8 * options.height;
    const diskPath = this.cachePath(sizedKey);
    if (diskPath) {
      try {
        const packed = await readFile(diskPath);
        if (packed.length === expectedBytes) {
          this.touch(sizedKey, packed);
          return packed;
        }
      } catch {
        // A cache miss or damaged entry simply falls through to regeneration.
      }
    }

    const input = await source();
    const { data } = await sharp(input)
      .rotate()
      .resize(options.width, options.height, {
        fit,
        position: fit === 'cover' ? 'attention' : 'centre',
        background: '#000000',
        kernel: sharp.kernel.lanczos3,
        fastShrinkOnLoad: false,
      })
      .greyscale()
      .normalise({ lower: 1, upper: 99 })
      .sharpen({ sigma: 0.85, m1: 0.5, m2: 2.5 })
      .raw()
      .toBuffer({ resolveWithObject: true });
    const packed = atkinsonDither(data, options.width, options.height);

    this.touch(sizedKey, packed);
    if (diskPath) await this.writeCache(diskPath, packed);
    return packed;
  }

  private touch(key: string, packed: Buffer): void {
    this.cache.delete(key);
    if (this.cache.size >= MAX_CACHE_ITEMS) {
      const oldest = this.cache.keys().next().value as string | undefined;
      if (oldest) this.cache.delete(oldest);
    }
    this.cache.set(key, packed);
  }

  private cachePath(key: string): string | null {
    if (!this.cacheDirectory) return null;
    const name = createHash('sha256').update(key).digest('hex');
    return join(this.cacheDirectory, `${name}.bin`);
  }

  private async writeCache(path: string, packed: Buffer): Promise<void> {
    const temporary = `${path}.${process.pid}.${randomUUID()}.tmp`;
    try {
      await mkdir(dirname(path), { recursive: true });
      await writeFile(temporary, packed);
      await rename(temporary, path);
    } catch {
      // Artwork is still usable when a read-only or full cache volume prevents persistence.
      await rm(temporary, { force: true }).catch(() => undefined);
    }
  }
}
