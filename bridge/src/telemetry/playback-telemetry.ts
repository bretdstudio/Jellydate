import type { ClientStats } from '../protocol/packet.js';

export type PlaybackTelemetryState =
  | 'connected'
  | 'ready'
  | 'buffering'
  | 'playing'
  | 'paused'
  | 'stopped'
  | 'ended'
  | 'error'
  | 'disconnected';

export interface PlaybackSessionSnapshot {
  readonly id: number;
  readonly remoteAddress: string;
  readonly connectedAt: string;
  readonly authenticatedAt?: string;
  readonly disconnectedAt?: string;
  readonly state: PlaybackTelemetryState;
  readonly active: boolean;
  readonly itemId?: string;
  readonly title?: string;
  readonly durationMs: number;
  readonly positionMs: number;
  readonly firstFrameAt?: string;
  readonly lastFrameAt?: string;
  readonly firstAudioAt?: string;
  readonly lastAudioAt?: string;
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
  readonly clientAppMode: number;
  readonly clientAudioPlayheadMs: number;
  readonly lastClientStatsAt?: string;
  readonly wireBytesSent: number;
  readonly backpressureEvents: number;
  readonly backpressureMs: number;
  readonly ffmpegStarts: number;
  readonly seeks: number;
  readonly pauses: number;
  readonly resumes: number;
  readonly jellyfinReportsSucceeded: number;
  readonly jellyfinReportsFailed: number;
  readonly lastJellyfinReportAt?: string;
  readonly error?: string;
}

export interface PlaybackTelemetrySnapshot {
  readonly serviceStartedAt: string;
  readonly serviceUptimeSeconds: number;
  readonly process: {
    readonly rssBytes: number;
    readonly heapUsedBytes: number;
    readonly externalBytes: number;
  };
  readonly session: PlaybackSessionSnapshot | null;
}

interface MutablePlaybackSession {
  id: number;
  remoteAddress: string;
  connectedAt: string;
  authenticatedAt?: string;
  disconnectedAt?: string;
  state: PlaybackTelemetryState;
  active: boolean;
  itemId?: string;
  title?: string;
  durationMs: number;
  positionMs: number;
  firstFrameAt?: string;
  lastFrameAt?: string;
  firstAudioAt?: string;
  lastAudioAt?: string;
  framesSent: number;
  maximumFrameGapMs: number;
  frameGapsOver100Ms: number;
  audioPacketsSent: number;
  audioWireBytesSent: number;
  clientQueuedVideoFrames: number;
  clientDroppedVideoFrames: number;
  clientQueuedAudioSamples: number;
  clientAudioUnderruns: number;
  clientDroppedAudioSamples: number;
  clientAppMode: number;
  clientAudioPlayheadMs: number;
  lastClientStatsAt?: string;
  wireBytesSent: number;
  backpressureEvents: number;
  backpressureMs: number;
  ffmpegStarts: number;
  seeks: number;
  pauses: number;
  resumes: number;
  jellyfinReportsSucceeded: number;
  jellyfinReportsFailed: number;
  lastJellyfinReportAt?: string;
  error?: string;
}

export class PlaybackTelemetry {
  private readonly startedAtMs = Date.now();
  private nextSessionId = 1;
  private session: MutablePlaybackSession | undefined;
  private lastFrameClockMs: number | undefined;

  openSession(remoteAddress: string): number {
    const id = this.nextSessionId++;
    this.session = {
      id,
      remoteAddress,
      connectedAt: nowIso(),
      state: 'connected',
      active: false,
      durationMs: 0,
      positionMs: 0,
      framesSent: 0,
      maximumFrameGapMs: 0,
      frameGapsOver100Ms: 0,
      audioPacketsSent: 0,
      audioWireBytesSent: 0,
      clientQueuedVideoFrames: 0,
      clientDroppedVideoFrames: 0,
      clientQueuedAudioSamples: 0,
      clientAudioUnderruns: 0,
      clientDroppedAudioSamples: 0,
      clientAppMode: 0,
      clientAudioPlayheadMs: 0,
      wireBytesSent: 0,
      backpressureEvents: 0,
      backpressureMs: 0,
      ffmpegStarts: 0,
      seeks: 0,
      pauses: 0,
      resumes: 0,
      jellyfinReportsSucceeded: 0,
      jellyfinReportsFailed: 0,
    };
    return id;
  }

  authenticated(id: number): void {
    const session = this.current(id);
    if (!session) return;
    session.authenticatedAt = nowIso();
    session.state = 'ready';
  }

  playbackStarted(
    id: number,
    itemId: string,
    title: string,
    durationMs: bigint,
    positionMs: bigint,
  ): void {
    const session = this.current(id);
    if (!session) return;
    session.active = true;
    session.state = 'buffering';
    session.itemId = itemId;
    session.title = title;
    session.durationMs = Number(durationMs);
    session.positionMs = Number(positionMs);
    session.ffmpegStarts += 1;
    this.lastFrameClockMs = undefined;
    delete session.error;
  }

  frameSent(id: number, positionMs: bigint, wireBytes: number): void {
    const session = this.current(id);
    if (!session) return;
    const clockMs = Date.now();
    if (this.lastFrameClockMs !== undefined) {
      const gapMs = clockMs - this.lastFrameClockMs;
      session.maximumFrameGapMs = Math.max(session.maximumFrameGapMs, gapMs);
      if (gapMs > 100) session.frameGapsOver100Ms += 1;
    }
    this.lastFrameClockMs = clockMs;
    const timestamp = nowIso();
    session.firstFrameAt ??= timestamp;
    session.lastFrameAt = timestamp;
    session.positionMs = Number(positionMs);
    session.framesSent += 1;
    session.wireBytesSent += wireBytes;
    session.state = 'playing';
  }

  controlBytesSent(id: number, wireBytes: number): void {
    const session = this.current(id);
    if (session) session.wireBytesSent += wireBytes;
  }

  audioSent(id: number, wireBytes: number): void {
    const session = this.current(id);
    if (!session) return;
    const timestamp = nowIso();
    session.firstAudioAt ??= timestamp;
    session.lastAudioAt = timestamp;
    session.audioPacketsSent += 1;
    session.audioWireBytesSent += wireBytes;
    session.wireBytesSent += wireBytes;
  }

  clientStats(id: number, stats: ClientStats): void {
    const session = this.current(id);
    if (!session) return;
    session.clientQueuedVideoFrames = stats.queuedVideoFrames;
    session.clientDroppedVideoFrames = stats.droppedVideoFrames;
    session.clientQueuedAudioSamples = stats.queuedAudioSamples;
    session.clientAudioUnderruns = stats.audioUnderruns;
    session.clientDroppedAudioSamples = stats.droppedAudioSamples;
    session.clientAppMode = stats.appMode;
    session.clientAudioPlayheadMs = stats.audioPlayheadMs;
    session.lastClientStatsAt = nowIso();
  }

  backpressure(id: number, durationMs: number): void {
    const session = this.current(id);
    if (!session) return;
    session.backpressureEvents += 1;
    session.backpressureMs += Math.max(0, Math.round(durationMs));
  }

  seek(id: number, positionMs: bigint): void {
    const session = this.current(id);
    if (!session) return;
    session.seeks += 1;
    session.positionMs = Number(positionMs);
    session.state = 'buffering';
  }

  pause(id: number): void {
    const session = this.current(id);
    if (!session) return;
    session.pauses += 1;
    session.state = 'paused';
    this.lastFrameClockMs = undefined;
  }

  resume(id: number): void {
    const session = this.current(id);
    if (!session) return;
    session.resumes += 1;
    session.state = 'playing';
    this.lastFrameClockMs = undefined;
  }

  jellyfinReport(id: number, succeeded: boolean): void {
    const session = this.current(id);
    if (!session) return;
    if (succeeded) session.jellyfinReportsSucceeded += 1;
    else session.jellyfinReportsFailed += 1;
    session.lastJellyfinReportAt = nowIso();
  }

  stopped(id: number, ended = false): void {
    const session = this.current(id);
    if (!session) return;
    session.active = false;
    session.state = ended ? 'ended' : 'stopped';
  }

  failed(id: number, message: string): void {
    const session = this.current(id);
    if (!session) return;
    session.active = false;
    session.state = 'error';
    session.error = message;
  }

  closeSession(id: number): void {
    const session = this.current(id);
    if (!session) return;
    session.active = false;
    session.state = 'disconnected';
    session.disconnectedAt = nowIso();
  }

  snapshot(): PlaybackTelemetrySnapshot {
    const memory = process.memoryUsage();
    return {
      serviceStartedAt: new Date(this.startedAtMs).toISOString(),
      serviceUptimeSeconds: Math.floor((Date.now() - this.startedAtMs) / 1000),
      process: {
        rssBytes: memory.rss,
        heapUsedBytes: memory.heapUsed,
        externalBytes: memory.external,
      },
      session: this.session ? { ...this.session } : null,
    };
  }

  private current(id: number): MutablePlaybackSession | undefined {
    return this.session?.id === id ? this.session : undefined;
  }
}

function nowIso(): string {
  return new Date().toISOString();
}
