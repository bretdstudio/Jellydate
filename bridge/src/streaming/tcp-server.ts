import { createServer, type Server } from 'node:net';
import type { Config } from '../config.js';
import type { JellyfinClient } from '../jellyfin/client.js';
import { StreamSession } from './stream-session.js';
import type { PlaybackTelemetry } from '../telemetry/playback-telemetry.js';

export function createStreamServer(
  config: Config,
  jellyfin: JellyfinClient,
  telemetry: PlaybackTelemetry,
  log: (message: string) => void,
): Server {
  return createServer((socket) => {
    socket.setNoDelay(true);
    socket.setKeepAlive(true, 10_000);
    const sessionId = telemetry.openSession(socket.remoteAddress ?? 'unknown');
    new StreamSession(socket, config, jellyfin, telemetry, sessionId, log).start();
  });
}
