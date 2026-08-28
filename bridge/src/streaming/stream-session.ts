import { timingSafeEqual } from 'node:crypto';
import type { Socket } from 'node:net';
import { once } from 'node:events';
import type { Config } from '../config.js';
import type { JellyfinClient, PlayableSource } from '../jellyfin/client.js';
import {
  AudioSampleFormat,
  HEADER_SIZE,
  PacketFlags,
  PacketType,
} from '../protocol/constants.js';
import {
  CatalogKind,
  decodeCatalogRequest,
  decodePlayCommand,
  decodeClientStats,
  encodeHomeItems,
  encodePacket,
  encodeStreamInfo,
  PacketParser,
  type Packet,
} from '../protocol/packet.js';
import { orderedDither8x8 } from '../transcoder/dither.js';
import { encodeFrameDelta } from '../transcoder/delta.js';
import { spawnRawVideo, type RawVideoProcess } from '../transcoder/ffmpeg.js';
import type { PlaybackTelemetry } from '../telemetry/playback-telemetry.js';
import { WireBudget } from './wire-budget.js';

const AUDIO_CHUNK_DURATION_MS = 40;
const MAX_AUDIO_LEAD_MS = 120n;

interface ActiveStream {
  readonly generation: number;
  readonly itemId: string;
  readonly startMs: bigint;
  readonly source?: PlayableSource;
  readonly ffmpeg: RawVideoProcess;
  positionMs: bigint;
  stopped: boolean;
  lastProgressReportAtMs: number;
  progressReportInFlight: boolean;
  paused: boolean;
  pausePromise?: Promise<void>;
  resumePlayback?: () => void;
  videoAdvancedPromise?: Promise<void>;
  signalVideoAdvanced?: () => void;
  pacingBaseClockMs?: number;
  pauseStartedClockMs?: number;
}

export class StreamSession {
  private readonly parser = new PacketParser();
  private authenticated = false;
  private sequence = 0;
  private generation = 0;
  private active: ActiveStream | undefined;
  private commandQueue = Promise.resolve();

  constructor(
    private readonly socket: Socket,
    private readonly config: Config,
    private readonly jellyfin: JellyfinClient,
    private readonly telemetry: PlaybackTelemetry,
    private readonly telemetrySessionId: number,
    private readonly log: (message: string) => void,
  ) {}

  start(): void {
    this.send(PacketType.Hello, Buffer.from('Jellydate Bridge/0.1 protocol/1'));
    this.socket.on('data', (chunk) => {
      try {
        for (const packet of this.parser.push(chunk)) {
          this.commandQueue = this.commandQueue
            .then(() => this.handle(packet))
            .catch((error: unknown) => this.fail(error));
        }
      } catch (error) {
        this.fail(error);
      }
    });
    this.socket.on('close', () => {
      void this.stopActive(true).finally(() => this.telemetry.closeSession(this.telemetrySessionId));
    });
    this.socket.on('error', (error) => this.log(`stream socket error: ${error.message}`));
  }

  private async handle(packet: Packet): Promise<void> {
    if (!this.authenticated && packet.type !== PacketType.Auth && packet.type !== PacketType.Ping) {
      throw new Error('Authenticate before sending stream commands');
    }

    switch (packet.type) {
      case PacketType.Auth:
        this.authenticate(packet.payload);
        break;
      case PacketType.Play:
        await this.play(decodePlayCommand(packet.payload));
        break;
      case PacketType.HomeRequest:
        await this.sendCatalog(decodeCatalogRequest(packet.payload));
        break;
      case PacketType.Pause:
        if (this.active) this.pauseActive(this.active);
        this.telemetry.pause(this.telemetrySessionId);
        this.send(PacketType.PlaybackState, Buffer.from('paused'));
        if (this.active) await this.reportProgress(this.active, true);
        break;
      case PacketType.Resume:
        if (this.active) this.resumeActive(this.active);
        this.telemetry.resume(this.telemetrySessionId);
        this.send(PacketType.PlaybackState, Buffer.from('playing'));
        if (this.active) await this.reportProgress(this.active, false);
        break;
      case PacketType.Seek:
        if (packet.payload.length !== 8 || !this.active) throw new Error('Invalid SEEK command');
        {
          const positionMs = packet.payload.readBigUInt64BE();
          this.telemetry.seek(this.telemetrySessionId, positionMs);
          await this.play({ itemId: this.active.itemId, startMs: positionMs });
        }
        break;
      case PacketType.Stop:
        await this.stopActive(false);
        this.telemetry.stopped(this.telemetrySessionId);
        break;
      case PacketType.Ping:
        this.send(PacketType.Pong, packet.payload, packet.timestampUs);
        break;
      case PacketType.ClientStats:
        this.telemetry.clientStats(this.telemetrySessionId, decodeClientStats(packet.payload));
        break;
      default:
        throw new Error(`Unexpected client packet type ${packet.type}`);
    }
  }

  private authenticate(payload: Buffer): void {
    const expected = Buffer.from(this.config.jellydateToken, 'utf8');
    this.authenticated = payload.length === expected.length && timingSafeEqual(payload, expected);
    if (!this.authenticated) {
      this.send(PacketType.Error, Buffer.from('ACCESS DENIED'));
      this.socket.destroy();
      return;
    }
    this.telemetry.authenticated(this.telemetrySessionId);
    this.send(PacketType.PlaybackState, Buffer.from('ready'));
  }

  private async sendCatalog(kind: CatalogKind): Promise<void> {
    let source;
    switch (kind) {
      case CatalogKind.Movies:
        source = await this.jellyfin.getMovies(8);
        break;
      case CatalogKind.Tv:
        source = await this.jellyfin.getTvEpisodes(8);
        break;
      case CatalogKind.RecentlyAdded:
        source = (await this.jellyfin.getHome()).recentlyAdded;
        break;
      default:
        source = (await this.jellyfin.getHome()).continueWatching;
        break;
    }
    this.send(
      PacketType.HomeResponse,
      encodeHomeItems(source.slice(0, 8).map((item) => ({
        id: item.id,
        title: item.seriesName ?? item.name,
        subtitle: item.seriesName
          ? [item.seasonName, item.name].filter(Boolean).join(' - ')
          : [item.type, item.productionYear].filter(Boolean).join(' - '),
        positionMs: BigInt(item.positionMs),
        durationMs: BigInt(item.durationMs),
      }))),
    );
  }

  private async play(command: { itemId: string; startMs: bigint }): Promise<void> {
    await this.stopActive(false, true);
    const generation = ++this.generation;

    let source: PlayableSource | undefined;
    const ffmpegInput = command.itemId === '__test__' && this.config.testMediaPath
      ? { path: this.config.testMediaPath }
      : { stream: (source = await this.jellyfin.openPlayableSource(command.itemId, command.startMs)).stream };
    const ffmpeg = spawnRawVideo(
      this.config.ffmpegPath,
      ffmpegInput,
      {
        fps: this.config.videoFps,
        videoWidth: this.config.videoWidth,
        videoHeight: this.config.videoHeight,
        audioSampleRate: this.config.audioSampleRate,
        audioSampleFormat: this.config.audioSampleFormat,
      },
      ffmpegInput.path ? command.startMs : 0n,
    );
    const active: ActiveStream = {
      generation,
      itemId: command.itemId,
      startMs: command.startMs,
      ...(source ? { source } : {}),
      ffmpeg,
      positionMs: command.startMs,
      stopped: false,
      lastProgressReportAtMs: Date.now(),
      progressReportInFlight: false,
      paused: false,
    };
    this.active = active;
    this.telemetry.playbackStarted(
      this.telemetrySessionId,
      command.itemId,
      source?.title ?? 'TEST TRANSMISSION',
      source?.durationMs ?? 0n,
      command.startMs,
    );

    this.send(
      PacketType.StreamInfo,
      encodeStreamInfo(
        {
          width: this.config.videoWidth,
          height: this.config.videoHeight,
          fps: this.config.videoFps,
          audioSampleRate: this.config.audioSampleRate,
          audioSampleFormat: this.config.audioSampleFormat === 's8'
            ? AudioSampleFormat.Signed8
            : AudioSampleFormat.Signed16LittleEndian,
        },
        source?.durationMs ?? 0n,
        source?.title ?? 'TEST TRANSMISSION',
        source?.viewport,
      ),
      command.startMs * 1000n,
      PacketFlags.Discontinuity,
    );
    this.send(PacketType.PlaybackState, Buffer.from('buffering'));
    if (source) {
      try {
        await this.jellyfin.reportStart(command.itemId, source, command.startMs);
        this.telemetry.jellyfinReport(this.telemetrySessionId, true);
      } catch (error) {
        this.telemetry.jellyfinReport(this.telemetrySessionId, false);
        throw error;
      }
    }

    let stderr = '';
    ffmpeg.process.stderr.on('data', (chunk: Buffer) => {
      if (stderr.length < 4_096) stderr += chunk.toString('utf8');
    });
    ffmpeg.process.on('error', (error) => this.fail(error));
    void this.pumpFrames(active).catch((error: unknown) => this.fail(error));
    void this.pumpAudio(active).catch((error: unknown) => this.fail(error));
    ffmpeg.process.on('close', (code) => {
      if (!active.stopped && this.active?.generation === generation) {
        if (code === 0) {
          this.send(PacketType.EndOfStream, Buffer.alloc(0), active.positionMs * 1000n);
        } else {
          this.fail(new Error(`FFmpeg exited with code ${code}: ${stderr.trim() || 'no diagnostic'}`));
        }
        void this.finishReporting(active);
      }
    });
  }

  private async pumpFrames(active: ActiveStream): Promise<void> {
    let pending = Buffer.alloc(0);
    let frameIndex = 0n;
    let previousFrame: Buffer | undefined;
    let playbackAnnounced = false;
    const rawFrameBytes = this.config.videoWidth * this.config.videoHeight;
    const frameDurationUs = 1_000_000n / BigInt(this.config.videoFps);
    const keyframeInterval = BigInt(
      this.config.videoFps * this.config.videoKeyframeIntervalSeconds,
    );
    const wireBudget = this.config.videoWireBudgetBytesPerSecond > 0
      ? new WireBudget(
          this.config.videoWireBudgetBytesPerSecond,
          Math.max(
            HEADER_SIZE + this.config.videoWidth * this.config.videoHeight / 8,
            this.config.videoWireBudgetBytesPerSecond / 2,
          ),
        )
      : undefined;

    for await (const chunk of active.ffmpeg.frames) {
      if (active.stopped || this.active?.generation !== active.generation) break;
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      pending = pending.length === 0 ? bytes : Buffer.concat([pending, bytes]);
      while (pending.length >= rawFrameBytes) {
        await this.waitIfPaused(active);
        if (active.stopped || this.active?.generation !== active.generation) return;
        const raw = pending.subarray(0, rawFrameBytes);
        pending = pending.subarray(rawFrameBytes);
        const timestampUs = active.startMs * 1000n + frameIndex * frameDurationUs;
        const positionMs = timestampUs / 1000n;
        await this.waitForPresentationTime(active, frameIndex, frameDurationUs);
        if (active.stopped || this.active?.generation !== active.generation) return;
        const packed = orderedDither8x8(
          raw,
          this.config.videoWidth,
          this.config.videoHeight,
          previousFrame,
          this.config.videoDitherHysteresis,
        );
        const delta = previousFrame && this.config.videoDeltaFrames
          ? encodeFrameDelta(previousFrame, packed, this.config.videoDeltaRepeatRuns)
          : undefined;
        const keyframeDue = frameIndex === 0n || frameIndex % keyframeInterval === 0n;
        const useDelta = !keyframeDue && delta !== undefined && delta.length < packed.length;
        const payload = useDelta ? delta : packed;
        if (wireBudget &&
            !wireBudget.trySpend(HEADER_SIZE + payload.length, performance.now(), keyframeDue)) {
          active.positionMs = positionMs;
          this.signalVideoAdvanced(active);
          frameIndex += 1n;
          this.maybeReportProgress(active);
          continue;
        }
        const packet = encodePacket({
          type: useDelta ? PacketType.VideoDelta : PacketType.VideoKeyframe,
          flags: frameIndex === 0n ? PacketFlags.Discontinuity : PacketFlags.None,
          timestampUs,
          sequence: this.sequence++,
          payload,
        });
        await this.writeMediaPacket(packet);
        previousFrame = packed;
        active.positionMs = positionMs;
        this.signalVideoAdvanced(active);
        frameIndex += 1n;
        this.telemetry.frameSent(this.telemetrySessionId, active.positionMs, packet.length);
        this.maybeReportProgress(active);
        if (!playbackAnnounced) {
          playbackAnnounced = true;
          this.send(PacketType.PlaybackState, Buffer.from('playing'));
        }
      }
    }
  }

  private async pumpAudio(active: ActiveStream): Promise<void> {
    let pending = Buffer.alloc(0);
    let sampleIndex = 0n;
    const sampleRate = BigInt(this.config.audioSampleRate);
    const chunkSamples = Math.floor(
      this.config.audioSampleRate * AUDIO_CHUNK_DURATION_MS / 1000,
    );
    const bytesPerSample = this.config.audioSampleFormat === 's8' ? 1 : 2;
    const chunkBytes = chunkSamples * bytesPerSample;

    for await (const chunk of active.ffmpeg.audio) {
      if (active.stopped || this.active?.generation !== active.generation) break;
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      pending = pending.length === 0 ? bytes : Buffer.concat([pending, bytes]);
      while (pending.length >= chunkBytes) {
        await this.waitIfPaused(active);
        if (active.stopped || this.active?.generation !== active.generation) return;
        const payload = Buffer.from(pending.subarray(0, chunkBytes));
        pending = pending.subarray(chunkBytes);
        const timestampUs = active.startMs * 1000n + sampleIndex * 1_000_000n / sampleRate;
        await this.waitForVideoTimeline(active, timestampUs / 1000n);
        if (active.stopped || this.active?.generation !== active.generation) return;
        const packet = encodePacket({
          type: PacketType.Audio,
          flags: sampleIndex === 0n ? PacketFlags.Discontinuity : PacketFlags.None,
          timestampUs,
          sequence: this.sequence++,
          payload,
        });
        await this.writeMediaPacket(packet);
        sampleIndex += BigInt(chunkSamples);
        this.telemetry.audioSent(this.telemetrySessionId, packet.length);
      }
    }
  }

  private async stopActive(disconnected: boolean, restarting = false): Promise<void> {
    const active = this.active;
    if (!active || active.stopped) return;
    active.stopped = true;
    this.active = undefined;
    this.resumeActive(active);
    this.signalVideoAdvanced(active);
    active.source?.stream.destroy();
    active.ffmpeg.process.kill('SIGTERM');
    if (active.source) {
      const reportStopped = async (): Promise<void> => {
        try {
          await this.jellyfin.reportStopped(active.itemId, active.source!, active.positionMs);
          this.telemetry.jellyfinReport(this.telemetrySessionId, true);
        } catch (error) {
          this.telemetry.jellyfinReport(this.telemetrySessionId, false);
          this.log(`could not report playback stop: ${safeMessage(error)}`);
        }
      };
      /* A Jellyfin progress report must not sit in the critical path between
         the crank settling and the replacement stream opening. */
      if (restarting) void reportStopped();
      else await reportStopped();
    }
    if (!disconnected && !restarting) this.send(PacketType.PlaybackState, Buffer.from('stopped'));
  }

  private async finishReporting(active: ActiveStream): Promise<void> {
    if (this.active?.generation === active.generation) this.active = undefined;
    this.telemetry.stopped(this.telemetrySessionId, true);
    if (active.source) {
      try {
        await this.jellyfin.reportStopped(active.itemId, active.source, active.positionMs);
        this.telemetry.jellyfinReport(this.telemetrySessionId, true);
      } catch (error) {
        this.telemetry.jellyfinReport(this.telemetrySessionId, false);
        this.log(`could not report playback completion: ${safeMessage(error)}`);
      }
    }
  }

  private send(type: PacketType, payload: Buffer, timestampUs = 0n, flags = 0): void {
    if (this.socket.destroyed) return;
    const packet = encodePacket({ type, flags, timestampUs, sequence: this.sequence++, payload });
    this.socket.write(packet);
    this.telemetry.controlBytesSent(this.telemetrySessionId, packet.length);
  }

  private maybeReportProgress(active: ActiveStream): void {
    if (!active.source || active.progressReportInFlight ||
        Date.now() - active.lastProgressReportAtMs < this.config.progressReportIntervalMs) return;
    active.lastProgressReportAtMs = Date.now();
    active.progressReportInFlight = true;
    void this.reportProgress(active, false).finally(() => {
      active.progressReportInFlight = false;
    });
  }

  private pauseActive(active: ActiveStream): void {
    if (active.paused) return;
    active.paused = true;
    active.pauseStartedClockMs = performance.now();
    active.pausePromise = new Promise<void>((resolve) => {
      active.resumePlayback = resolve;
    });
  }

  private resumeActive(active: ActiveStream): void {
    if (!active.paused) return;
    const resume = active.resumePlayback;
    if (active.pacingBaseClockMs !== undefined && active.pauseStartedClockMs !== undefined) {
      active.pacingBaseClockMs += performance.now() - active.pauseStartedClockMs;
    }
    active.paused = false;
    delete active.pauseStartedClockMs;
    delete active.pausePromise;
    delete active.resumePlayback;
    resume?.();
  }

  private async waitIfPaused(active: ActiveStream): Promise<void> {
    if (active.pausePromise) await active.pausePromise;
  }

  private async waitForVideoTimeline(active: ActiveStream, audioPositionMs: bigint): Promise<void> {
    while (!active.stopped && this.active?.generation === active.generation &&
           audioPositionMs > active.positionMs + MAX_AUDIO_LEAD_MS) {
      active.videoAdvancedPromise ??= new Promise<void>((resolve) => {
        active.signalVideoAdvanced = resolve;
      });
      await active.videoAdvancedPromise;
    }
  }

  private async waitForPresentationTime(
    active: ActiveStream,
    frameIndex: bigint,
    frameDurationUs: bigint,
  ): Promise<void> {
    active.pacingBaseClockMs ??= performance.now();
    const offsetMs = Number(frameIndex * frameDurationUs) / 1000;
    while (!active.stopped && this.active?.generation === active.generation) {
      await this.waitIfPaused(active);
      const dueAtMs = active.pacingBaseClockMs + offsetMs;
      const remainingMs = dueAtMs - performance.now();
      if (remainingMs <= 0) return;
      await sleep(remainingMs);
    }
  }

  private signalVideoAdvanced(active: ActiveStream): void {
    const signal = active.signalVideoAdvanced;
    delete active.videoAdvancedPromise;
    delete active.signalVideoAdvanced;
    signal?.();
  }

  private async writeMediaPacket(packet: Buffer): Promise<void> {
    if (this.socket.write(packet)) return;
    const stalledAt = Date.now();
    await once(this.socket, 'drain');
    this.telemetry.backpressure(this.telemetrySessionId, Date.now() - stalledAt);
  }

  private async reportProgress(active: ActiveStream, paused: boolean): Promise<void> {
    if (!active.source) return;
    try {
      await this.jellyfin.reportProgress(
        active.itemId,
        active.source,
        active.positionMs,
        paused,
      );
      this.telemetry.jellyfinReport(this.telemetrySessionId, true);
    } catch (error) {
      this.telemetry.jellyfinReport(this.telemetrySessionId, false);
      this.log(`could not report playback progress: ${safeMessage(error)}`);
    }
  }

  private fail(error: unknown): void {
    const message = safeMessage(error);
    this.log(`stream failed: ${message}`);
    this.telemetry.failed(this.telemetrySessionId, message);
    this.send(PacketType.Error, Buffer.from(message.slice(0, 240)));
    void this.stopActive(false, true);
  }
}

function safeMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function sleep(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
