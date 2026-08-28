import sharp from 'sharp';
import { orderedDither8x8 } from '../transcoder/dither.js';

const MAX_CACHE_ITEMS = 64;

export class PosterService {
  private readonly cache = new Map<string, Buffer>();

  async convert(itemId: string, source: () => Promise<Buffer>): Promise<Buffer> {
    const cached = this.cache.get(itemId);
    if (cached) return cached;

    const input = await source();
    const { data } = await sharp(input)
      .resize(400, 240, { fit: 'contain', background: '#000000' })
      .greyscale()
      .gamma(1.05)
      .raw()
      .toBuffer({ resolveWithObject: true });
    const packed = orderedDither8x8(data);

    if (this.cache.size >= MAX_CACHE_ITEMS) {
      const oldest = this.cache.keys().next().value as string | undefined;
      if (oldest) this.cache.delete(oldest);
    }
    this.cache.set(itemId, packed);
    return packed;
  }
}

