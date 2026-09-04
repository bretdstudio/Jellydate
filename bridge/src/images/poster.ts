import sharp from 'sharp';
import { orderedDither8x8 } from '../transcoder/dither.js';

const MAX_CACHE_ITEMS = 64;

export interface PosterOptions {
  readonly width: number;
  readonly height: number;
  readonly fit?: 'contain' | 'cover';
}

export class PosterService {
  private readonly cache = new Map<string, Buffer>();

  async convert(
    cacheKey: string,
    source: () => Promise<Buffer>,
    options: PosterOptions = { width: 400, height: 240, fit: 'contain' },
  ): Promise<Buffer> {
    const fit = options.fit ?? 'cover';
    const sizedKey = `${cacheKey}:${options.width}x${options.height}:${fit}`;
    const cached = this.cache.get(sizedKey);
    if (cached) return cached;

    const input = await source();
    const { data } = await sharp(input)
      .resize(options.width, options.height, {
        fit,
        position: fit === 'cover' ? 'attention' : 'centre',
        background: '#000000',
      })
      .greyscale()
      .gamma(1.05)
      .sharpen({ sigma: 0.7 })
      .raw()
      .toBuffer({ resolveWithObject: true });
    const packed = orderedDither8x8(data, options.width, options.height);

    if (this.cache.size >= MAX_CACHE_ITEMS) {
      const oldest = this.cache.keys().next().value as string | undefined;
      if (oldest) this.cache.delete(oldest);
    }
    this.cache.set(sizedKey, packed);
    return packed;
  }
}
