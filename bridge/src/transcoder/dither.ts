import { SCREEN_HEIGHT, SCREEN_WIDTH } from '../protocol/constants.js';

// An 8x8 Bayer matrix is spatially stable, cheap, and avoids the moving-image
// shimmer produced by propagating error between pixels and frames.
const BAYER_8 = new Uint8Array([
   0, 48, 12, 60,  3, 51, 15, 63,
  32, 16, 44, 28, 35, 19, 47, 31,
   8, 56,  4, 52, 11, 59,  7, 55,
  40, 24, 36, 20, 43, 27, 39, 23,
   2, 50, 14, 62,  1, 49, 13, 61,
  34, 18, 46, 30, 33, 17, 45, 29,
  10, 58,  6, 54,  9, 57,  5, 53,
  42, 26, 38, 22, 41, 25, 37, 21,
]);

export function orderedDither8x8(
  gray: Uint8Array,
  width = SCREEN_WIDTH,
  height = SCREEN_HEIGHT,
  previous?: Uint8Array,
  hysteresis = 0,
): Buffer {
  if (width < 8 || width % 8 !== 0 || height < 1) {
    throw new Error('Dither dimensions require a positive height and a width divisible by 8');
  }
  if (gray.length !== width * height) {
    throw new Error(`Expected ${width * height} grayscale bytes, got ${gray.length}`);
  }
  const packedRowBytes = width / 8;
  if (previous && previous.length !== packedRowBytes * height) {
    throw new Error(`Expected ${packedRowBytes * height} previous packed bytes, got ${previous.length}`);
  }
  if (!Number.isInteger(hysteresis) || hysteresis < 0 || hysteresis > 64) {
    throw new Error('Dither hysteresis must be an integer from 0 to 64');
  }

  const packed = Buffer.alloc(packedRowBytes * height);
  for (let y = 0; y < height; y += 1) {
    const inputRow = y * width;
    const outputRow = y * packedRowBytes;
    for (let x = 0; x < width; x += 8) {
      let byte = 0;
      for (let bit = 0; bit < 8; bit += 1) {
        const px = x + bit;
        let threshold = ((BAYER_8[(y & 7) * 8 + (px & 7)] ?? 0) + 0.5) * 4;
        if (previous && hysteresis > 0) {
          const wasWhite = ((previous[outputRow + (px >> 3)] ?? 0) & (0x80 >> (px & 7))) !== 0;
          threshold += wasWhite ? -hysteresis : hysteresis;
          threshold = Math.max(0, Math.min(255, threshold));
        }
        if ((gray[inputRow + px] ?? 0) >= threshold) byte |= 0x80 >> bit;
      }
      packed[outputRow + (x >> 3)] = byte;
    }
  }
  return packed;
}

// Static artwork benefits from error diffusion: unlike video, it never shimmers
// between frames, and the propagated error preserves faces, lettering and fine
// tonal changes much better than a repeating ordered pattern.
export function atkinsonDither(
  gray: Uint8Array,
  width: number,
  height: number,
): Buffer {
  if (width < 8 || width % 8 !== 0 || height < 1) {
    throw new Error('Dither dimensions require a positive height and a width divisible by 8');
  }
  if (gray.length !== width * height) {
    throw new Error(`Expected ${width * height} grayscale bytes, got ${gray.length}`);
  }

  const pixels = Float32Array.from(gray);
  const packedRowBytes = width / 8;
  const packed = Buffer.alloc(packedRowBytes * height);

  const diffuse = (x: number, y: number, error: number): void => {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const index = y * width + x;
    pixels[index] = Math.max(0, Math.min(255, (pixels[index] ?? 0) + error));
  };

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const index = y * width + x;
      const original = pixels[index] ?? 0;
      const white = original >= 128;
      const quantized = white ? 255 : 0;
      if (white) packed[y * packedRowBytes + (x >> 3)]! |= 0x80 >> (x & 7);

      const error = (original - quantized) / 8;
      diffuse(x + 1, y, error);
      diffuse(x + 2, y, error);
      diffuse(x - 1, y + 1, error);
      diffuse(x, y + 1, error);
      diffuse(x + 1, y + 1, error);
      diffuse(x, y + 2, error);
    }
  }

  return packed;
}
