import 'dotenv/config';

export interface Config {
  readonly jellyfinUrl: string;
  readonly jellyfinUsername: string;
  readonly jellyfinPassword: string;
  readonly bridgeHost: string;
  readonly bridgePort: number;
  readonly streamPort: number;
  readonly jellydateToken: string;
  readonly videoFps: number;
  readonly videoWidth: number;
  readonly videoHeight: number;
  readonly videoDeltaFrames: boolean;
  readonly videoDeltaRepeatRuns: boolean;
  readonly videoKeyframeIntervalSeconds: number;
  readonly videoWireBudgetBytesPerSecond: number;
  readonly videoDitherHysteresis: number;
  readonly audioSampleRate: number;
  readonly audioSampleFormat: 's8' | 's16le';
  readonly progressReportIntervalMs: number;
  readonly ffmpegPath: string;
  readonly testMediaPath?: string;
}

function required(name: string): string {
  const value = process.env[name]?.trim();
  if (!value) throw new Error(`${name} is required`);
  return value;
}

function port(name: string, fallback: number): number {
  const value = Number(process.env[name] ?? fallback);
  if (!Number.isInteger(value) || value < 1 || value > 65_535) {
    throw new Error(`${name} must be an integer from 1 to 65535`);
  }
  return value;
}

function boundedInteger(name: string, fallback: number, minimum: number, maximum: number): number {
  const value = Number(process.env[name] ?? fallback);
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${name} must be an integer from ${minimum} to ${maximum}`);
  }
  return value;
}

function booleanValue(name: string, fallback: boolean): boolean {
  const value = process.env[name]?.trim().toLowerCase();
  if (value === undefined || value === '') return fallback;
  if (value === 'true' || value === '1') return true;
  if (value === 'false' || value === '0') return false;
  throw new Error(`${name} must be true or false`);
}

export function loadConfig(): Config {
  const jellydateToken = required('JELLYDATE_TOKEN');
  if (jellydateToken.length < 16) {
    throw new Error('JELLYDATE_TOKEN must be at least 16 characters');
  }

  const videoFps = Number(process.env.VIDEO_FPS ?? 15);
  if (!Number.isInteger(videoFps) || videoFps < 1 || videoFps > 30) {
    throw new Error('VIDEO_FPS must be an integer from 1 to 30');
  }

  const videoWidth = boundedInteger('VIDEO_WIDTH', 400, 8, 400);
  const videoHeight = boundedInteger('VIDEO_HEIGHT', 240, 1, 240);
  if (videoWidth % 8 !== 0 || videoWidth * 240 !== videoHeight * 400) {
    throw new Error(
      'VIDEO_WIDTH/VIDEO_HEIGHT must fit the 400x240 display at a width divisible by 8',
    );
  }
  const audioSampleRate = boundedInteger('AUDIO_SAMPLE_RATE', 22_050, 8_000, 22_050);
  const audioSampleFormat = process.env.AUDIO_SAMPLE_FORMAT?.trim() || 's16le';
  if (audioSampleFormat !== 's8' && audioSampleFormat !== 's16le') {
    throw new Error('AUDIO_SAMPLE_FORMAT must be s8 or s16le');
  }

  const testMediaPath = process.env.TEST_MEDIA_PATH?.trim();
  return {
    jellyfinUrl: required('JELLYFIN_URL').replace(/\/$/, ''),
    jellyfinUsername: required('JELLYFIN_USERNAME'),
    jellyfinPassword: required('JELLYFIN_PASSWORD'),
    bridgeHost: process.env.BRIDGE_HOST?.trim() || '127.0.0.1',
    bridgePort: port('BRIDGE_PORT', 7789),
    streamPort: port('STREAM_PORT', 7790),
    jellydateToken,
    videoFps,
    videoWidth,
    videoHeight,
    videoDeltaFrames: booleanValue('VIDEO_DELTA_FRAMES', false),
    videoDeltaRepeatRuns: booleanValue('VIDEO_DELTA_REPEAT_RUNS', false),
    videoKeyframeIntervalSeconds: boundedInteger(
      'VIDEO_KEYFRAME_INTERVAL_SECONDS',
      2,
      1,
      60,
    ),
    videoWireBudgetBytesPerSecond: boundedInteger(
      'VIDEO_WIRE_BUDGET_KIB_PER_SECOND',
      0,
      0,
      128,
    ) * 1024,
    videoDitherHysteresis: boundedInteger('VIDEO_DITHER_HYSTERESIS', 0, 0, 32),
    audioSampleRate,
    audioSampleFormat,
    progressReportIntervalMs: boundedInteger(
      'PROGRESS_REPORT_INTERVAL_MS',
      10_000,
      5_000,
      300_000,
    ),
    ffmpegPath: process.env.FFMPEG_PATH?.trim() || 'ffmpeg',
    ...(testMediaPath ? { testMediaPath } : {}),
  };
}
