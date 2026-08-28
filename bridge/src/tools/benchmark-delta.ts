import 'dotenv/config';

import { loadConfig } from '../config.js';
import { JellyfinClient } from '../jellyfin/client.js';
import { encodeFrameDelta } from '../transcoder/delta.js';
import { orderedDither8x8 } from '../transcoder/dither.js';
import { spawnRawVideo, type FfmpegInput } from '../transcoder/ffmpeg.js';

const seconds = numericArgument('--seconds', 30);
const keyframeSeconds = numericArgument('--keyframe-seconds', 2);
const fpsValues = stringArgument('--fps', '4,5,6')
  .split(',')
  .map(Number)
  .filter((value) => Number.isInteger(value) && value > 0 && value <= 30);
if (fpsValues.length === 0) throw new Error('--fps must contain at least one valid frame rate');
const config = loadConfig();
const hysteresis = numericArgument('--hysteresis', config.videoDitherHysteresis || 0, true);
const mediaPath = config.testMediaPath;
let jellyfin: JellyfinClient | undefined;
let itemId: string | undefined;
if (!mediaPath) {
  const response = await fetch(`http://127.0.0.1:${config.bridgePort}/api/telemetry`, {
    headers: { 'x-jellydate-token': config.jellydateToken },
  });
  if (response.ok) {
    const snapshot = await response.json() as { session?: { itemId?: string } };
    itemId = snapshot.session?.itemId;
  }
  if (!itemId || itemId === '__test__') {
    throw new Error('Start a Jellyfin item on Playdate before running the delta benchmark');
  }
  jellyfin = new JellyfinClient(config, 'jellydate-delta-benchmark-v1');
  await jellyfin.connect();
}

for (const fps of fpsValues) {
  const input: FfmpegInput = mediaPath
    ? { path: mediaPath }
    : { stream: (await jellyfin!.openPlayableSource(itemId!, 0n)).stream };
  const result = await benchmark(input, fps, seconds, keyframeSeconds, hysteresis);
  process.stdout.write(
    `${fps} FPS/${keyframeSeconds}s keys/h${hysteresis}: ${result.frames} frames, ` +
    `${result.keyframes} keyframes, ` +
    `${result.deltas} deltas, delta median ${formatBytes(result.deltaMedian)}, ` +
    `p95 ${formatBytes(result.deltaP95)}, video ${formatRate(result.videoBytesPerSecond)}, ` +
    `estimated A/V ${formatRate(result.totalBytesPerSecond)}\n`,
  );
}

async function benchmark(
  input: FfmpegInput,
  fps: number,
  durationSeconds: number,
  keyframeDurationSeconds: number,
  ditherHysteresis: number,
): Promise<{
  frames: number;
  keyframes: number;
  deltas: number;
  deltaMedian: number;
  deltaP95: number;
  videoBytesPerSecond: number;
  totalBytesPerSecond: number;
}> {
  const width = 400;
  const height = 240;
  const rawFrameBytes = width * height;
  const targetFrames = fps * durationSeconds;
  const keyframeInterval = Math.max(1, Math.round(fps * keyframeDurationSeconds));
  const transcoder = spawnRawVideo('ffmpeg', input, {
    fps,
    videoWidth: width,
    videoHeight: height,
    audioSampleRate: 8_000,
    audioSampleFormat: 's8',
  }, 0n);
  const drainAudio = (async () => {
    for await (const _chunk of transcoder.audio) {
      // Drain the independent FFmpeg pipe so it cannot block video output.
    }
  })();

  let pending = Buffer.alloc(0);
  let previous: Buffer | undefined;
  let frames = 0;
  let keyframes = 0;
  let deltas = 0;
  let videoWireBytes = 0;
  const deltaSizes: number[] = [];

  outer: for await (const chunk of transcoder.frames) {
    const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    pending = pending.length === 0 ? bytes : Buffer.concat([pending, bytes]);
    while (pending.length >= rawFrameBytes) {
      const raw = pending.subarray(0, rawFrameBytes);
      pending = pending.subarray(rawFrameBytes);
      const packed = orderedDither8x8(raw, width, height, previous, ditherHysteresis);
      const delta = previous
        ? encodeFrameDelta(previous, packed, config.videoDeltaRepeatRuns)
        : undefined;
      const keyframeDue = frames === 0 || frames % keyframeInterval === 0;
      if (!keyframeDue && delta && delta.length < packed.length) {
        videoWireBytes += 24 + delta.length;
        deltaSizes.push(delta.length);
        deltas += 1;
      } else {
        videoWireBytes += 24 + packed.length;
        keyframes += 1;
      }
      previous = packed;
      frames += 1;
      if (frames >= targetFrames) break outer;
    }
  }
  transcoder.process.kill('SIGTERM');
  await drainAudio;

  deltaSizes.sort((left, right) => left - right);
  const observedSeconds = frames / fps;
  const videoBytesPerSecond = videoWireBytes / observedSeconds;
  const audioBytesPerSecond = 8_000 + 25 * 24;
  return {
    frames,
    keyframes,
    deltas,
    deltaMedian: percentile(deltaSizes, 0.5),
    deltaP95: percentile(deltaSizes, 0.95),
    videoBytesPerSecond,
    totalBytesPerSecond: videoBytesPerSecond + audioBytesPerSecond,
  };
}

function percentile(sorted: number[], fraction: number): number {
  if (sorted.length === 0) return 0;
  return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * fraction))] ?? 0;
}

function numericArgument(name: string, fallback: number, allowZero = false): number {
  const value = Number(stringArgument(name, String(fallback)));
  if (!Number.isFinite(value) || (allowZero ? value < 0 : value <= 0)) {
    throw new Error(`${name} must be ${allowZero ? 'non-negative' : 'positive'}`);
  }
  return value;
}

function stringArgument(name: string, fallback: string): string {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] ?? fallback : fallback;
}

function formatBytes(bytes: number): string {
  return bytes < 1024 ? `${Math.round(bytes)} B` : `${(bytes / 1024).toFixed(1)} KiB`;
}

function formatRate(bytesPerSecond: number): string {
  return `${(bytesPerSecond / 1024).toFixed(1)} KiB/s`;
}
