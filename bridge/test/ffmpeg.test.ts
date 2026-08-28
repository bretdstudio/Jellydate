import { execFile } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { PACKED_FRAME_BYTES, SCREEN_HEIGHT, SCREEN_WIDTH } from '../src/protocol/constants.js';
import { orderedDither8x8 } from '../src/transcoder/dither.js';
import { spawnRawVideo } from '../src/transcoder/ffmpeg.js';

const execFileAsync = promisify(execFile);
let directory = '';
let videoPath = '';

beforeAll(async () => {
  directory = await mkdtemp(join(tmpdir(), 'jellydate-ffmpeg-'));
  videoPath = join(directory, 'test.mp4');
  await execFileAsync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error',
    '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=30',
    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
    '-t', '0.5', '-shortest', '-pix_fmt', 'yuv420p', videoPath,
  ]);
});

afterAll(async () => {
  if (directory) await rm(directory, { recursive: true });
});

describe('FFmpeg raw-video pipeline', () => {
  it('produces fixed-size video frames and 22.05 kHz mono PCM', async () => {
    const transcoder = spawnRawVideo('ffmpeg', { path: videoPath }, {
      fps: 15,
      videoWidth: SCREEN_WIDTH,
      videoHeight: SCREEN_HEIGHT,
      audioSampleRate: 22_050,
      audioSampleFormat: 's16le',
    }, 0n);
    const collect = async (stream: NodeJS.ReadableStream): Promise<Buffer> => {
      const chunks: Buffer[] = [];
      for await (const chunk of stream) chunks.push(Buffer.from(chunk));
      return Buffer.concat(chunks);
    };
    const [output, audio] = await Promise.all([
      collect(transcoder.frames),
      collect(transcoder.audio),
    ]);
    const frameBytes = SCREEN_WIDTH * SCREEN_HEIGHT;
    expect(output.length).toBeGreaterThanOrEqual(frameBytes * 5);
    expect(output.length % frameBytes).toBe(0);
    expect(orderedDither8x8(output.subarray(0, frameBytes))).toHaveLength(PACKED_FRAME_BYTES);
    expect(audio.length).toBeGreaterThanOrEqual(20_000);
    expect(audio.length % 2).toBe(0);
    expect(audio.some((sample) => sample !== 0)).toBe(true);
  });

  it('produces the low-bandwidth hardware profile', async () => {
    const width = 200;
    const height = 120;
    const transcoder = spawnRawVideo('ffmpeg', { path: videoPath }, {
      fps: 6,
      videoWidth: width,
      videoHeight: height,
      audioSampleRate: 8_000,
      audioSampleFormat: 's8',
    }, 0n);
    const collect = async (stream: NodeJS.ReadableStream): Promise<Buffer> => {
      const chunks: Buffer[] = [];
      for await (const chunk of stream) chunks.push(Buffer.from(chunk));
      return Buffer.concat(chunks);
    };
    const [output, audio] = await Promise.all([
      collect(transcoder.frames),
      collect(transcoder.audio),
    ]);
    const frameBytes = width * height;
    expect(output.length).toBeGreaterThanOrEqual(frameBytes * 2);
    expect(output.length % frameBytes).toBe(0);
    expect(orderedDither8x8(output.subarray(0, frameBytes), width, height)).toHaveLength(3_000);
    expect(audio.length).toBeGreaterThanOrEqual(3_000);
    expect(audio.length).toBeLessThan(6_000);
    expect(audio.some((sample) => sample !== 0)).toBe(true);
  });

  it('produces the balanced 240x144 hardware profile', async () => {
    const width = 240;
    const height = 144;
    const transcoder = spawnRawVideo('ffmpeg', { path: videoPath }, {
      fps: 5,
      videoWidth: width,
      videoHeight: height,
      audioSampleRate: 8_000,
      audioSampleFormat: 's8',
    }, 0n);
    const collect = async (stream: NodeJS.ReadableStream): Promise<Buffer> => {
      const chunks: Buffer[] = [];
      for await (const chunk of stream) chunks.push(Buffer.from(chunk));
      return Buffer.concat(chunks);
    };
    const [output, audio] = await Promise.all([
      collect(transcoder.frames),
      collect(transcoder.audio),
    ]);
    const frameBytes = width * height;
    expect(output.length).toBeGreaterThanOrEqual(frameBytes * 2);
    expect(output.length % frameBytes).toBe(0);
    expect(orderedDither8x8(output.subarray(0, frameBytes), width, height)).toHaveLength(4_320);
    expect(audio.length).toBeGreaterThanOrEqual(3_000);
    expect(audio.length).toBeLessThan(6_000);
  });
});
