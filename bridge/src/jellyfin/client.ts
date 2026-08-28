import { Jellyfin } from '@jellyfin/sdk/lib/jellyfin.js';
import type { Api } from '@jellyfin/sdk/lib/api.js';
import type { BaseItemDto } from '@jellyfin/sdk/lib/generated-client/models/base-item-dto.js';
import type { MediaSourceInfo } from '@jellyfin/sdk/lib/generated-client/models/media-source-info.js';
import type { PlaybackInfoResponse } from '@jellyfin/sdk/lib/generated-client/models/playback-info-response.js';
import { getImageApi } from '@jellyfin/sdk/lib/utils/api/image-api.js';
import { getItemsApi } from '@jellyfin/sdk/lib/utils/api/items-api.js';
import { getMediaInfoApi } from '@jellyfin/sdk/lib/utils/api/media-info-api.js';
import { getPlaystateApi } from '@jellyfin/sdk/lib/utils/api/playstate-api.js';
import { getUserApi } from '@jellyfin/sdk/lib/utils/api/user-api.js';
import { getUserLibraryApi } from '@jellyfin/sdk/lib/utils/api/user-library-api.js';
import { getUserViewsApi } from '@jellyfin/sdk/lib/utils/api/user-views-api.js';
import { Readable } from 'node:stream';
import type { Config } from '../config.js';
import { SCREEN_HEIGHT, SCREEN_WIDTH } from '../protocol/constants.js';
import type { StreamViewport } from '../protocol/packet.js';

const TICKS_PER_MS = 10_000;

export interface JellydateItem {
  readonly id: string;
  readonly name: string;
  readonly type: string;
  readonly productionYear: number | null;
  readonly durationMs: number;
  readonly positionMs: number;
  readonly playedPercentage: number;
  readonly imageTag: string | null;
  readonly seriesName: string | null;
  readonly seasonName: string | null;
}

export interface PlayableSource {
  readonly mediaSourceId: string;
  readonly playSessionId: string;
  readonly durationMs: bigint;
  readonly title: string;
  readonly viewport: StreamViewport;
  readonly stream: Readable;
}

function ticksToMs(ticks: number | null | undefined): number {
  return Math.max(0, Math.floor((ticks ?? 0) / TICKS_PER_MS));
}

function compactItem(item: BaseItemDto): JellydateItem | null {
  if (!item.Id || !item.Name) return null;
  return {
    id: item.Id,
    name: item.Name,
    type: item.Type ?? 'Unknown',
    productionYear: item.ProductionYear ?? null,
    durationMs: ticksToMs(item.RunTimeTicks),
    positionMs: ticksToMs(item.UserData?.PlaybackPositionTicks),
    playedPercentage: item.UserData?.PlayedPercentage ?? 0,
    imageTag: item.ImageTags?.Primary ?? item.SeriesPrimaryImageTag ?? null,
    seriesName: item.SeriesName ?? null,
    seasonName: item.SeasonName ?? null,
  };
}

function compactItems(items: BaseItemDto[] | null | undefined): JellydateItem[] {
  return (items ?? []).map(compactItem).filter((item): item is JellydateItem => item !== null);
}

export class JellyfinClient {
  private readonly api: Api;
  private userId = '';
  private userName = '';

  constructor(
    private readonly config: Config,
    deviceId = 'jellydate-bridge-v1',
  ) {
    const jellyfin = new Jellyfin({
      clientInfo: { name: 'Jellydate Bridge', version: '0.1.0' },
      deviceInfo: { name: 'Jellydate Bridge', id: deviceId },
    });
    this.api = jellyfin.createApi(config.jellyfinUrl);
  }

  async connect(): Promise<void> {
    const response = await getUserApi(this.api).authenticateUserByName({
      authenticateUserByName: {
        Username: this.config.jellyfinUsername,
        Pw: this.config.jellyfinPassword,
      },
    });
    const user = response.data.User;
    if (!user?.Id || !user.Name) throw new Error('Jellyfin authentication returned no user');
    this.userId = user.Id;
    this.userName = user.Name;
  }

  get currentUser(): { id: string; name: string } {
    if (!this.userId) throw new Error('Jellyfin client is not connected');
    return { id: this.userId, name: this.userName };
  }

  async getHome(): Promise<{ continueWatching: JellydateItem[]; recentlyAdded: JellydateItem[] }> {
    const itemsApi = getItemsApi(this.api);
    const [resume, recent] = await Promise.all([
      itemsApi.getResumeItems({
        userId: this.currentUser.id,
        limit: 12,
        mediaTypes: ['Video'],
        includeItemTypes: ['Movie', 'Episode'],
        enableImages: true,
        enableUserData: true,
      }),
      itemsApi.getItems({
        userId: this.currentUser.id,
        limit: 12,
        recursive: true,
        includeItemTypes: ['Movie', 'Episode'],
        sortBy: ['DateCreated'],
        sortOrder: ['Descending'],
        enableImages: true,
        enableUserData: true,
      }),
    ]);
    return {
      continueWatching: compactItems(resume.data.Items),
      recentlyAdded: compactItems(recent.data.Items),
    };
  }

  async getLibraries(): Promise<JellydateItem[]> {
    const response = await getUserViewsApi(this.api).getUserViews({
      userId: this.currentUser.id,
      includeHidden: false,
    });
    return compactItems(response.data.Items);
  }

  async getMovies(limit = 8): Promise<JellydateItem[]> {
    const boundedLimit = Number.isFinite(limit)
      ? Math.min(Math.max(Math.floor(limit), 1), 100)
      : 8;
    const response = await getItemsApi(this.api).getItems({
      userId: this.currentUser.id,
      limit: boundedLimit,
      recursive: true,
      includeItemTypes: ['Movie'],
      sortBy: ['SortName'],
      sortOrder: ['Ascending'],
      enableImages: true,
      enableUserData: true,
    });
    return compactItems(response.data.Items);
  }

  async getLibrary(parentId: string, limit = 50): Promise<JellydateItem[]> {
    const boundedLimit = Number.isFinite(limit)
      ? Math.min(Math.max(Math.floor(limit), 1), 100)
      : 50;
    const response = await getItemsApi(this.api).getItems({
      userId: this.currentUser.id,
      parentId,
      limit: boundedLimit,
      recursive: true,
      includeItemTypes: ['Movie', 'Series', 'Season', 'Episode'],
      sortBy: ['SortName'],
      sortOrder: ['Ascending'],
      enableImages: true,
      enableUserData: true,
    });
    return compactItems(response.data.Items);
  }

  async getItem(itemId: string): Promise<JellydateItem> {
    const response = await getUserLibraryApi(this.api).getItem({
      itemId,
      userId: this.currentUser.id,
    });
    const item = compactItem(response.data);
    if (!item) throw new Error('Jellyfin returned an item without an id or name');
    return item;
  }

  async getPrimaryImage(itemId: string): Promise<Buffer> {
    const response = await getImageApi(this.api).getItemImage(
      { itemId, imageType: 'Primary', maxWidth: 400, maxHeight: 240, quality: 90 },
      { responseType: 'arraybuffer' },
    );
    return Buffer.from(response.data as unknown as ArrayBuffer);
  }

  async openPlayableSource(itemId: string, startMs = 0n): Promise<PlayableSource> {
    const item = await this.getItem(itemId);
    const playback = await getMediaInfoApi(this.api).getPlaybackInfo({
      itemId,
      userId: this.currentUser.id,
    });
    const selected = selectMediaSource(playback.data);
    if (!selected.Id) throw new Error('Jellyfin media source has no id');
    const videoStream = selected.MediaStreams?.find((stream) => stream.Type === 'Video');

    const response = await this.api.axiosInstance.get<Readable>(
      this.api.getUri(`/Videos/${encodeURIComponent(itemId)}/stream`),
      {
        params: {
          MediaSourceId: selected.Id,
          PlaySessionId: playback.data.PlaySessionId,
          ...(startMs > 0n
            ? {
                /* Jellyfin ignores StartTimeTicks for a static/original-file
                   response. Its dynamic stream endpoint performs the seek and
                   gives FFmpeg media that actually begins at the requested
                   position. */
                Static: false,
                StartTimeTicks: Number(startMs) * TICKS_PER_MS,
                Container: 'ts',
                VideoCodec: 'h264',
                AudioCodec: 'aac',
              }
            : { Static: true }),
        },
        responseType: 'stream',
      },
    );
    return {
      mediaSourceId: selected.Id,
      playSessionId: playback.data.PlaySessionId ?? '',
      durationMs: BigInt(item.durationMs),
      title: item.seriesName ? `${item.seriesName} - ${item.name}` : item.name,
      viewport: fitViewport(videoStream?.Width, videoStream?.Height, videoStream?.AspectRatio),
      stream: response.data,
    };
  }

  async reportStart(itemId: string, source: PlayableSource, positionMs: bigint): Promise<void> {
    await getPlaystateApi(this.api).reportPlaybackStart({
      playbackStartInfo: playbackInfo(itemId, source, positionMs, false),
    });
  }

  async reportProgress(
    itemId: string,
    source: PlayableSource,
    positionMs: bigint,
    paused: boolean,
  ): Promise<void> {
    await getPlaystateApi(this.api).reportPlaybackProgress({
      playbackProgressInfo: playbackInfo(itemId, source, positionMs, paused),
    });
  }

  async reportStopped(itemId: string, source: PlayableSource, positionMs: bigint): Promise<void> {
    await getPlaystateApi(this.api).reportPlaybackStopped({
      playbackStopInfo: {
        ItemId: itemId,
        MediaSourceId: source.mediaSourceId,
        PlaySessionId: source.playSessionId,
        PositionTicks: Number(positionMs) * TICKS_PER_MS,
        Failed: false,
      },
    });
  }
}

function fitViewport(
  width: number | null | undefined,
  height: number | null | undefined,
  aspectRatio: string | null | undefined,
): StreamViewport {
  const ratioMatch = aspectRatio?.match(/^\s*(\d+(?:\.\d+)?)\s*:\s*(\d+(?:\.\d+)?)\s*$/);
  const reportedRatio = ratioMatch
    ? Number(ratioMatch[1]) / Number(ratioMatch[2])
    : 0;
  const aspect = reportedRatio > 0
    ? reportedRatio
    : (width && height && width > 0 && height > 0 ? width / height : 0);
  if (!Number.isFinite(aspect) || aspect <= 0) {
    return { x: 0, y: 0, width: SCREEN_WIDTH, height: SCREEN_HEIGHT };
  }

  const screenAspect = SCREEN_WIDTH / SCREEN_HEIGHT;
  const fittedWidth = aspect >= screenAspect
    ? SCREEN_WIDTH
    : Math.max(1, Math.round(SCREEN_HEIGHT * aspect));
  const fittedHeight = aspect >= screenAspect
    ? Math.max(1, Math.round(SCREEN_WIDTH / aspect))
    : SCREEN_HEIGHT;
  return {
    x: Math.floor((SCREEN_WIDTH - fittedWidth) / 2),
    y: Math.floor((SCREEN_HEIGHT - fittedHeight) / 2),
    width: fittedWidth,
    height: fittedHeight,
  };
}

function selectMediaSource(playback: PlaybackInfoResponse): MediaSourceInfo {
  const source = playback.MediaSources?.find((candidate) => candidate.SupportsDirectStream)
    ?? playback.MediaSources?.[0];
  if (!source) throw new Error(`Jellyfin could not provide a playable source (${playback.ErrorCode ?? 'unknown'})`);
  return source;
}

function playbackInfo(
  itemId: string,
  source: PlayableSource,
  positionMs: bigint,
  paused: boolean,
) {
  return {
    ItemId: itemId,
    MediaSourceId: source.mediaSourceId,
    PlaySessionId: source.playSessionId,
    PositionTicks: Number(positionMs) * TICKS_PER_MS,
    CanSeek: true,
    IsPaused: paused,
    IsMuted: false,
    PlayMethod: 'DirectPlay' as const,
  };
}
