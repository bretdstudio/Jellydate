import { loadConfig } from './config.js';
import { buildApp } from './app.js';
import { JellyfinClient } from './jellyfin/client.js';
import { createStreamServer } from './streaming/tcp-server.js';
import { PlaybackTelemetry } from './telemetry/playback-telemetry.js';

async function main(): Promise<void> {
  const config = loadConfig();
  const jellyfin = new JellyfinClient(config);
  await jellyfin.connect();
  const telemetry = new PlaybackTelemetry();

  const app = buildApp(config, jellyfin, telemetry);
  const streamServer = createStreamServer(
    config,
    jellyfin,
    telemetry,
    (message) => app.log.info(message),
  );
  await new Promise<void>((resolve, reject) => {
    streamServer.once('error', reject);
    streamServer.listen(config.streamPort, config.bridgeHost, resolve);
  });
  await app.listen({ host: config.bridgeHost, port: config.bridgePort });
  app.log.info(`tuned to Jellyfin as ${jellyfin.currentUser.name}`);
  app.log.info(`Jellydate stream listening on ${config.bridgeHost}:${config.streamPort}`);

  const close = async () => {
    streamServer.close();
    await app.close();
  };
  process.once('SIGINT', () => void close());
  process.once('SIGTERM', () => void close());
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  process.stderr.write(`Jellydate Bridge failed: ${message}\n`);
  process.exitCode = 1;
});
