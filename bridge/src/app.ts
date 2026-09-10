import Fastify, { type FastifyInstance } from 'fastify';
import type { Config } from './config.js';
import type { JellyfinClient } from './jellyfin/client.js';
import { PosterService } from './images/poster.js';
import { PROTOCOL_VERSION } from './protocol/constants.js';
import type { PlaybackTelemetry } from './telemetry/playback-telemetry.js';

export function buildApp(
  config: Config,
  jellyfin: JellyfinClient,
  telemetry: PlaybackTelemetry,
  posters: PosterService,
): FastifyInstance {
  const app = Fastify({ logger: { redact: ['req.headers.x-jellydate-token', 'req.headers.authorization'] } });

  app.get('/health', async () => ({
    ok: true,
    service: 'jellydate-bridge',
    protocolVersion: PROTOCOL_VERSION,
    streamPort: config.streamPort,
  }));

  app.addHook('onRequest', async (request, reply) => {
    if (!request.url.startsWith('/api/')) return;
    if (request.headers['x-jellydate-token'] !== config.jellydateToken) {
      await reply.code(401).send({ error: 'ACCESS DENIED' });
    }
  });

  app.get('/api/home', async () => jellyfin.getHome());
  app.get('/api/movies', async () => ({ items: await jellyfin.getMovies(50) }));
  app.get('/api/tv', async () => ({ items: await jellyfin.getTvSeries(50) }));
  app.get<{ Params: { id: string } }>('/api/tv/:id/seasons', async (request) => ({
    items: await jellyfin.getSeasons(request.params.id, 50),
  }));
  app.get<{ Params: { id: string } }>('/api/tv/seasons/:id/episodes', async (request) => ({
    items: await jellyfin.getEpisodes(request.params.id, 50),
  }));
  app.get('/api/telemetry', async () => telemetry.snapshot());
  app.get('/api/libraries', async () => ({ items: await jellyfin.getLibraries() }));
  app.get<{ Params: { id: string }; Querystring: { limit?: string } }>(
    '/api/libraries/:id/items',
    async (request) => ({
      items: await jellyfin.getLibrary(request.params.id, Number(request.query.limit ?? 50)),
    }),
  );
  app.get<{ Params: { id: string } }>('/api/items/:id', async (request) => ({
    item: await jellyfin.getItem(request.params.id),
  }));
  app.get<{ Params: { id: string } }>('/api/items/:id/image', async (request, reply) => {
    const item = await jellyfin.getItem(request.params.id);
    if (!item.imageItemId || !item.imageTag) {
      return reply.code(404).send({ error: 'ARTWORK NOT AVAILABLE' });
    }
    const image = await posters.convert(
      `${item.imageItemId}:${item.imageTag}`,
      () => jellyfin.getPrimaryImage(item.imageItemId!, 1600, 960),
      { width: 400, height: 240, fit: 'contain' },
    );
    return reply
      .header('content-type', 'application/vnd.jellydate.bitmap')
      .header('x-jellydate-width', '400')
      .header('x-jellydate-height', '240')
      .header('cache-control', 'private, max-age=86400')
      .send(image);
  });

  return app;
}
