import { describe, expect, it } from 'vitest';
import { PlaybackTelemetry } from '../src/telemetry/playback-telemetry.js';

describe('playback telemetry', () => {
  it('tracks a bounded snapshot of the active stream', () => {
    const telemetry = new PlaybackTelemetry();
    const sessionId = telemetry.openSession('127.0.0.1');

    telemetry.authenticated(sessionId);
    telemetry.playbackStarted(sessionId, 'movie-id', 'The Tiny Movie', 10_800_000n, 500n);
    telemetry.frameSent(sessionId, 566n, 12_024);
    telemetry.audioSent(sessionId, 1_788);
    telemetry.clientStats(sessionId, {
      queuedVideoFrames: 4,
      droppedVideoFrames: 3,
      queuedAudioSamples: 5_292,
      audioUnderruns: 1,
      droppedAudioSamples: 2,
      appMode: 2,
      audioPlayheadMs: 12_345,
    });
    telemetry.backpressure(sessionId, 7.6);
    telemetry.seek(sessionId, 3_600_000n);
    telemetry.pause(sessionId);
    telemetry.resume(sessionId);
    telemetry.jellyfinReport(sessionId, true);
    telemetry.jellyfinReport(sessionId, false);

    const snapshot = telemetry.snapshot();
    expect(snapshot.process.rssBytes).toBeGreaterThan(0);
    expect(snapshot.session).toMatchObject({
      id: sessionId,
      remoteAddress: '127.0.0.1',
      state: 'playing',
      active: true,
      itemId: 'movie-id',
      title: 'The Tiny Movie',
      durationMs: 10_800_000,
      positionMs: 3_600_000,
      framesSent: 1,
      maximumFrameGapMs: 0,
      frameGapsOver100Ms: 0,
      audioPacketsSent: 1,
      audioWireBytesSent: 1_788,
      clientQueuedVideoFrames: 4,
      clientDroppedVideoFrames: 3,
      clientQueuedAudioSamples: 5_292,
      clientAudioUnderruns: 1,
      clientDroppedAudioSamples: 2,
      clientAppMode: 2,
      clientAudioPlayheadMs: 12_345,
      wireBytesSent: 13_812,
      backpressureEvents: 1,
      backpressureMs: 8,
      ffmpegStarts: 1,
      seeks: 1,
      pauses: 1,
      resumes: 1,
      jellyfinReportsSucceeded: 1,
      jellyfinReportsFailed: 1,
    });
    expect(snapshot.session?.firstAudioAt).toBeTruthy();
    expect(snapshot.session?.lastAudioAt).toBeTruthy();
    expect(snapshot.session?.lastClientStatsAt).toBeTruthy();
  });

  it('does not let an obsolete connection mutate the current session', () => {
    const telemetry = new PlaybackTelemetry();
    const oldSession = telemetry.openSession('old');
    const currentSession = telemetry.openSession('current');

    telemetry.failed(oldSession, 'stale failure');
    telemetry.authenticated(currentSession);

    expect(telemetry.snapshot().session).toMatchObject({
      id: currentSession,
      remoteAddress: 'current',
      state: 'ready',
    });
  });
});
