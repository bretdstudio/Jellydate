import 'dotenv/config';

import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import type {
  PlaybackSessionSnapshot,
  PlaybackTelemetrySnapshot,
} from '../telemetry/playback-telemetry.js';

interface SoakOptions {
  readonly durationMs: number;
  readonly intervalMs: number;
  readonly startupGraceMs: number;
  readonly stallMs: number;
  readonly maxRssGrowthBytes: number;
  readonly endpoint: string;
  readonly token: string;
  readonly reportDirectory: string;
}

interface SoakSample {
  readonly capturedAt: string;
  readonly state: string;
  readonly active: boolean;
  readonly positionMs: number;
  readonly framesSent: number;
  readonly maximumFrameGapMs: number;
  readonly frameGapsOver100Ms: number;
  readonly audioPacketsSent: number;
  readonly audioWireBytesSent: number;
  readonly clientQueuedVideoFrames: number;
  readonly clientDroppedVideoFrames: number;
  readonly clientQueuedAudioSamples: number;
  readonly clientAudioUnderruns: number;
  readonly clientDroppedAudioSamples: number;
  readonly wireBytesSent: number;
  readonly backpressureEvents: number;
  readonly backpressureMs: number;
  readonly jellyfinReportsFailed: number;
  readonly lastFrameAt?: string;
  readonly lastAudioAt?: string;
  readonly rssBytes: number;
  readonly heapUsedBytes: number;
}

interface SoakReport {
  passed: boolean;
  startedAt: string;
  endedAt: string;
  requestedDurationSeconds: number;
  observedDurationSeconds: number;
  endpoint: string;
  failure?: string;
  summary: {
    samples: number;
    startPositionMs: number;
    endPositionMs: number;
    framesSent: number;
    maximumFrameGapMs: number;
    frameGapsOver100Ms: number;
    audioPacketsSent: number;
    audioWireBytesSent: number;
    clientDroppedVideoFrames: number;
    clientAudioUnderruns: number;
    clientDroppedAudioSamples: number;
    wireBytesSent: number;
    backpressureEvents: number;
    backpressureMs: number;
    jellyfinReportFailures: number;
    baselineRssBytes: number;
    peakRssBytes: number;
    rssGrowthBytes: number;
  };
  samples: SoakSample[];
}

const options = parseOptions(process.argv.slice(2));
const startedAtMs = Date.now();
const startedAt = new Date(startedAtMs).toISOString();
const samples: SoakSample[] = [];
let firstSession: PlaybackSessionSnapshot | undefined;
let lastSession: PlaybackSessionSnapshot | undefined;
let baselineRssBytes = 0;
let peakRssBytes = 0;
let failure: string | undefined;
let playingObserved = false;
let lastStatusLogAt = 0;

process.stdout.write(
  `Jellydate soak: ${formatDuration(options.durationMs)} against ${options.endpoint}\n` +
  `Waiting up to ${formatDuration(options.startupGraceMs)} for active playback...\n`,
);

while (Date.now() - startedAtMs < options.durationMs) {
  try {
    const snapshot = await fetchTelemetry(options);
    const session = snapshot.session;
    if (!session) {
      if (Date.now() - startedAtMs > options.startupGraceMs) {
        throw new Error('no Playdate stream session connected before startup grace expired');
      }
      await sleep(options.intervalMs);
      continue;
    }

    firstSession ??= session;
    lastSession = session;
    baselineRssBytes ||= snapshot.process.rssBytes;
    peakRssBytes = Math.max(peakRssBytes, snapshot.process.rssBytes);
    const sample = toSample(snapshot, session);
    samples.push(sample);

    if (session.state === 'playing') playingObserved = true;
    if (!playingObserved && Date.now() - startedAtMs > options.startupGraceMs) {
      throw new Error(`playback did not reach playing state (last state: ${session.state})`);
    }
    if (playingObserved && ['error', 'disconnected', 'stopped', 'ended'].includes(session.state)) {
      throw new Error(`playback entered terminal state: ${session.state}${session.error ? ` (${session.error})` : ''}`);
    }
    if (playingObserved && session.lastFrameAt &&
        Date.now() - Date.parse(session.lastFrameAt) > options.stallMs) {
      throw new Error(`no frame sent for more than ${formatDuration(options.stallMs)}`);
    }
    if (playingObserved && session.lastAudioAt &&
        Date.now() - Date.parse(session.lastAudioAt) > options.stallMs) {
      throw new Error(`no audio sent for more than ${formatDuration(options.stallMs)}`);
    }
    if (baselineRssBytes > 0 && snapshot.process.rssBytes - baselineRssBytes > options.maxRssGrowthBytes) {
      throw new Error(
        `bridge RSS grew by more than ${formatBytes(options.maxRssGrowthBytes)} ` +
        `(now ${formatBytes(snapshot.process.rssBytes)})`,
      );
    }

    if (Date.now() - lastStatusLogAt >= 30_000) {
      lastStatusLogAt = Date.now();
      process.stdout.write(
        `${elapsed(startedAtMs)}  ${session.state.padEnd(9)}  ` +
        `position ${formatClock(session.positionMs)}  frames ${session.framesSent}  ` +
        `audio ${session.audioPacketsSent}  ` +
        `rss ${formatBytes(snapshot.process.rssBytes)}  ` +
        `backpressure ${session.backpressureEvents}/${session.backpressureMs}ms\n`,
      );
    }
  } catch (error) {
    failure = safeMessage(error);
    break;
  }
  await sleep(options.intervalMs);
}

const endedAtMs = Date.now();
const startFrames = firstSession?.framesSent ?? 0;
const endFrames = lastSession?.framesSent ?? startFrames;
const startBytes = firstSession?.wireBytesSent ?? 0;
const endBytes = lastSession?.wireBytesSent ?? startBytes;
const startAudioPackets = firstSession?.audioPacketsSent ?? 0;
const endAudioPackets = lastSession?.audioPacketsSent ?? startAudioPackets;
const startAudioBytes = firstSession?.audioWireBytesSent ?? 0;
const endAudioBytes = lastSession?.audioWireBytesSent ?? startAudioBytes;
const startVideoDrops = firstSession?.clientDroppedVideoFrames ?? 0;
const endVideoDrops = lastSession?.clientDroppedVideoFrames ?? startVideoDrops;
const startAudioUnderruns = firstSession?.clientAudioUnderruns ?? 0;
const endAudioUnderruns = lastSession?.clientAudioUnderruns ?? startAudioUnderruns;
const startAudioDrops = firstSession?.clientDroppedAudioSamples ?? 0;
const endAudioDrops = lastSession?.clientDroppedAudioSamples ?? startAudioDrops;
const startBackpressureEvents = firstSession?.backpressureEvents ?? 0;
const endBackpressureEvents = lastSession?.backpressureEvents ?? startBackpressureEvents;
const startBackpressureMs = firstSession?.backpressureMs ?? 0;
const endBackpressureMs = lastSession?.backpressureMs ?? startBackpressureMs;
const startReportFailures = firstSession?.jellyfinReportsFailed ?? 0;
const endReportFailures = lastSession?.jellyfinReportsFailed ?? startReportFailures;
const report: SoakReport = {
  passed: !failure && playingObserved,
  startedAt,
  endedAt: new Date(endedAtMs).toISOString(),
  requestedDurationSeconds: options.durationMs / 1000,
  observedDurationSeconds: (endedAtMs - startedAtMs) / 1000,
  endpoint: options.endpoint,
  ...(failure ? { failure } : {}),
  summary: {
    samples: samples.length,
    startPositionMs: firstSession?.positionMs ?? 0,
    endPositionMs: lastSession?.positionMs ?? 0,
    framesSent: Math.max(0, endFrames - startFrames),
    maximumFrameGapMs: lastSession?.maximumFrameGapMs ?? 0,
    frameGapsOver100Ms: Math.max(
      0,
      (lastSession?.frameGapsOver100Ms ?? 0) - (firstSession?.frameGapsOver100Ms ?? 0),
    ),
    audioPacketsSent: Math.max(0, endAudioPackets - startAudioPackets),
    audioWireBytesSent: Math.max(0, endAudioBytes - startAudioBytes),
    clientDroppedVideoFrames: Math.max(0, endVideoDrops - startVideoDrops),
    clientAudioUnderruns: Math.max(0, endAudioUnderruns - startAudioUnderruns),
    clientDroppedAudioSamples: Math.max(0, endAudioDrops - startAudioDrops),
    wireBytesSent: Math.max(0, endBytes - startBytes),
    backpressureEvents: Math.max(0, endBackpressureEvents - startBackpressureEvents),
    backpressureMs: Math.max(0, endBackpressureMs - startBackpressureMs),
    jellyfinReportFailures: Math.max(0, endReportFailures - startReportFailures),
    baselineRssBytes,
    peakRssBytes,
    rssGrowthBytes: Math.max(0, peakRssBytes - baselineRssBytes),
  },
  samples,
};

await mkdir(options.reportDirectory, { recursive: true });
const reportPath = path.join(
  options.reportDirectory,
  `soak-${startedAt.replace(/[:.]/g, '-')}.json`,
);
await writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');

process.stdout.write(
  `${report.passed ? 'PASS' : 'FAIL'} after ${formatDuration(endedAtMs - startedAtMs)}: ` +
  `${report.summary.framesSent} frames, ${report.summary.audioPacketsSent} audio packets, ` +
  `${formatBytes(report.summary.wireBytesSent)}, ` +
  `max frame gap ${report.summary.maximumFrameGapMs} ms, ` +
  `client drops ${report.summary.clientDroppedVideoFrames}v/${report.summary.clientDroppedAudioSamples}a, ` +
  `underruns ${report.summary.clientAudioUnderruns}, ` +
  `RSS growth ${formatBytes(report.summary.rssGrowthBytes)}.\n` +
  `Report: ${reportPath}\n`,
);
if (!report.passed) {
  process.stderr.write(`${failure ?? 'playback never reached playing state'}\n`);
  process.exitCode = 1;
}

async function fetchTelemetry(soak: SoakOptions): Promise<PlaybackTelemetrySnapshot> {
  const response = await fetch(soak.endpoint, {
    headers: { 'x-jellydate-token': soak.token },
    signal: AbortSignal.timeout(Math.min(soak.intervalMs, 5_000)),
  });
  if (!response.ok) throw new Error(`telemetry returned HTTP ${response.status}`);
  return await response.json() as PlaybackTelemetrySnapshot;
}

function toSample(
  snapshot: PlaybackTelemetrySnapshot,
  session: PlaybackSessionSnapshot,
): SoakSample {
  return {
    capturedAt: new Date().toISOString(),
    state: session.state,
    active: session.active,
    positionMs: session.positionMs,
    framesSent: session.framesSent,
    maximumFrameGapMs: session.maximumFrameGapMs,
    frameGapsOver100Ms: session.frameGapsOver100Ms,
    audioPacketsSent: session.audioPacketsSent,
    audioWireBytesSent: session.audioWireBytesSent,
    clientQueuedVideoFrames: session.clientQueuedVideoFrames,
    clientDroppedVideoFrames: session.clientDroppedVideoFrames,
    clientQueuedAudioSamples: session.clientQueuedAudioSamples,
    clientAudioUnderruns: session.clientAudioUnderruns,
    clientDroppedAudioSamples: session.clientDroppedAudioSamples,
    wireBytesSent: session.wireBytesSent,
    backpressureEvents: session.backpressureEvents,
    backpressureMs: session.backpressureMs,
    jellyfinReportsFailed: session.jellyfinReportsFailed,
    ...(session.lastFrameAt ? { lastFrameAt: session.lastFrameAt } : {}),
    ...(session.lastAudioAt ? { lastAudioAt: session.lastAudioAt } : {}),
    rssBytes: snapshot.process.rssBytes,
    heapUsedBytes: snapshot.process.heapUsedBytes,
  };
}

function parseOptions(args: string[]): SoakOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index];
    const value = args[index + 1];
    if (!key?.startsWith('--') || value === undefined) {
      throw new Error('soak arguments must be --name value pairs');
    }
    values.set(key.slice(2), value);
  }

  const host = process.env.BRIDGE_HOST?.trim() || '127.0.0.1';
  const reachableHost = host === '0.0.0.0' || host === '::' ? '127.0.0.1' : host;
  const port = positiveNumber(process.env.BRIDGE_PORT ?? '7789', 'BRIDGE_PORT');
  const seconds = values.has('seconds')
    ? positiveNumber(values.get('seconds'), '--seconds')
    : positiveNumber(values.get('minutes') ?? '30', '--minutes') * 60;
  const intervalSeconds = positiveNumber(values.get('interval-seconds') ?? '5', '--interval-seconds');
  const reportDirectory = path.resolve(
    values.get('report-directory') ?? path.join(process.cwd(), 'soak-results'),
  );
  const token = process.env.JELLYDATE_TOKEN?.trim();
  if (!token) throw new Error('JELLYDATE_TOKEN is required');

  return {
    durationMs: seconds * 1000,
    intervalMs: intervalSeconds * 1000,
    startupGraceMs: positiveNumber(values.get('startup-grace-seconds') ?? '30', '--startup-grace-seconds') * 1000,
    stallMs: positiveNumber(values.get('stall-seconds') ?? '20', '--stall-seconds') * 1000,
    maxRssGrowthBytes: positiveNumber(values.get('max-rss-growth-mb') ?? '128', '--max-rss-growth-mb') * 1024 * 1024,
    endpoint: values.get('url') ?? `http://${reachableHost}:${port}/api/telemetry`,
    token,
    reportDirectory,
  };
}

function positiveNumber(value: string | undefined, label: string): number {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) throw new Error(`${label} must be a positive number`);
  return parsed;
}

function sleep(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function elapsed(startedAtMs: number): string {
  return `[${formatDuration(Date.now() - startedAtMs).padStart(8)}]`;
}

function formatDuration(milliseconds: number): string {
  const totalSeconds = Math.floor(milliseconds / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return hours > 0
    ? `${hours}h ${minutes}m ${seconds}s`
    : minutes > 0 ? `${minutes}m ${seconds}s` : `${seconds}s`;
}

function formatClock(milliseconds: number): string {
  const totalSeconds = Math.floor(milliseconds / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return [hours, minutes, seconds].map((value) => String(value).padStart(2, '0')).join(':');
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
}

function safeMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
