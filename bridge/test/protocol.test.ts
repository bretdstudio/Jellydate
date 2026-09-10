import { describe, expect, it } from 'vitest';
import {
  AudioSampleFormat,
  PacketFlags,
  PacketType,
  PlaybackStatus,
} from '../src/protocol/constants.js';
import {
  CatalogKind,
  decodeCatalogRequest,
  decodeItemDetailsRequest,
  decodePlayCommand,
  decodeClientStats,
  encodeHomeItems,
  encodeItemArtwork,
  encodeItemDetails,
  encodeItemDetailsRequest,
  encodePacket,
  encodePlayCommand,
  encodeStreamInfo,
  PacketParser,
} from '../src/protocol/packet.js';

describe('Jellydate packet protocol', () => {
  it('survives arbitrarily fragmented TCP reads', () => {
    const wire = encodePacket({
      type: PacketType.VideoKeyframe,
      flags: PacketFlags.Discontinuity,
      timestampUs: 1_234_567n,
      sequence: 42,
      payload: Buffer.from([1, 2, 3, 4]),
    });
    const parser = new PacketParser();
    const packets = [
      ...parser.push(wire.subarray(0, 3)),
      ...parser.push(wire.subarray(3, 17)),
      ...parser.push(wire.subarray(17, 25)),
      ...parser.push(wire.subarray(25)),
    ];

    expect(packets).toHaveLength(1);
    expect(packets[0]).toMatchObject({
      type: PacketType.VideoKeyframe,
      flags: PacketFlags.Discontinuity,
      timestampUs: 1_234_567n,
      sequence: 42,
    });
    expect(packets[0]?.payload).toEqual(Buffer.from([1, 2, 3, 4]));
  });

  it('round-trips a PLAY command without JSON', () => {
    const command = { itemId: '9f21c3', startMs: 2_482_000n };
    expect(decodePlayCommand(encodePlayCommand(command))).toEqual(command);
  });

  it('encodes a bounded compact home list', () => {
    const payload = encodeHomeItems([{
      id: 'episode-id',
      title: 'The Jelly Files',
      subtitle: 'Season 2 - A Wobbly Case',
      positionMs: 1_234n,
      durationMs: 5_678n,
      playbackStatus: PlaybackStatus.InProgress,
    }]);
    let cursor = 2;
    const idLength = payload.readUInt8(cursor++);
    expect(payload.readUInt8(0)).toBe(1);
    expect(payload.readUInt8(1)).toBe(0);
    expect(payload.toString('utf8', cursor, cursor + idLength)).toBe('episode-id');
    cursor += idLength;
    const titleLength = payload.readUInt8(cursor++);
    expect(payload.toString('utf8', cursor, cursor + titleLength)).toBe('The Jelly Files');
    cursor += titleLength;
    const subtitleLength = payload.readUInt8(cursor++);
    expect(payload.toString('utf8', cursor, cursor + subtitleLength)).toBe('Season 2 - A Wobbly Case');
    cursor += subtitleLength;
    expect(payload.readBigUInt64BE(cursor)).toBe(1_234n);
    expect(payload.readBigUInt64BE(cursor + 8)).toBe(5_678n);
    expect(payload.readUInt8(cursor + 16)).toBe(PlaybackStatus.InProgress);
  });

  it('decodes catalog requests and rejects unknown catalogs', () => {
    expect(decodeCatalogRequest(Buffer.from([CatalogKind.ContinueWatching])))
      .toEqual({ kind: CatalogKind.ContinueWatching, parentId: '', startIndex: 0 });
    expect(decodeCatalogRequest(Buffer.from([CatalogKind.Movies, 0])))
      .toEqual({ kind: CatalogKind.Movies, parentId: '', startIndex: 0 });
    expect(decodeCatalogRequest(Buffer.from([
      CatalogKind.Movies, 1, ...Buffer.from('M'), 0, 24,
    ]))).toEqual({ kind: CatalogKind.Movies, parentId: 'M', startIndex: 24 });
    expect(decodeCatalogRequest(Buffer.from([CatalogKind.Tv])))
      .toEqual({ kind: CatalogKind.Tv, parentId: '', startIndex: 0 });
    expect(decodeCatalogRequest(Buffer.from([
      CatalogKind.Tv, 1, ...Buffer.from('S'), 0, 8,
    ]))).toEqual({ kind: CatalogKind.Tv, parentId: 'S', startIndex: 8 });
    expect(decodeCatalogRequest(Buffer.from([CatalogKind.RecentlyAdded])))
      .toEqual({ kind: CatalogKind.RecentlyAdded, parentId: '', startIndex: 0 });
    expect(decodeCatalogRequest(Buffer.from([CatalogKind.TvSeasons, 6, ...Buffer.from('series')])))
      .toEqual({ kind: CatalogKind.TvSeasons, parentId: 'series', startIndex: 0 });
    expect(decodeCatalogRequest(Buffer.from([
      CatalogKind.TvEpisodes, 6, ...Buffer.from('season'), 0, 16,
    ]))).toEqual({ kind: CatalogKind.TvEpisodes, parentId: 'season', startIndex: 16 });
    expect(() => decodeCatalogRequest(Buffer.alloc(0))).toThrow(/empty/);
    expect(() => decodeCatalogRequest(Buffer.from([CatalogKind.TvSeasons, 4, 1]))).toThrow(/length/);
    expect(() => decodeCatalogRequest(Buffer.from([99]))).toThrow(/Unknown catalog/);
  });

  it('marks catalog responses that have another page', () => {
    expect(encodeHomeItems([], true)).toEqual(Buffer.from([0, 1]));
  });

  it('encodes item details and validates detail requests', () => {
    const request = encodeItemDetailsRequest('movie-id');
    expect(decodeItemDetailsRequest(request)).toBe('movie-id');
    expect(() => decodeItemDetailsRequest(Buffer.from([4, 1]))).toThrow(/length/);

    const payload = encodeItemDetails({
      title: 'The Jelly Files',
      subtitle: 'Movie - 2026',
      overview: 'A tiny television attempts the impossible.',
      positionMs: 12_345n,
      durationMs: 98_765n,
      playbackStatus: PlaybackStatus.Completed,
    });
    let cursor = 0;
    const titleLength = payload.readUInt8(cursor++);
    expect(payload.toString('utf8', cursor, cursor + titleLength)).toBe('The Jelly Files');
    cursor += titleLength;
    const subtitleLength = payload.readUInt8(cursor++);
    expect(payload.toString('utf8', cursor, cursor + subtitleLength)).toBe('Movie - 2026');
    cursor += subtitleLength;
    const overviewLength = payload.readUInt16BE(cursor);
    cursor += 2;
    expect(payload.toString('utf8', cursor, cursor + overviewLength))
      .toBe('A tiny television attempts the impossible.');
    cursor += overviewLength;
    expect(payload.readBigUInt64BE(cursor)).toBe(12_345n);
    expect(payload.readBigUInt64BE(cursor + 8)).toBe(98_765n);
    expect(payload.readUInt8(cursor + 16)).toBe(PlaybackStatus.Completed);
  });

  it('encodes compact artwork and an explicit unavailable fallback', () => {
    const packed = Buffer.alloc(12 * 144, 0xa5);
    const payload = encodeItemArtwork({ width: 96, height: 144, packed });
    expect(payload.readUInt8(0)).toBe(1);
    expect(payload.readUInt16BE(1)).toBe(96);
    expect(payload.readUInt16BE(3)).toBe(144);
    expect(payload.subarray(5)).toEqual(packed);
    expect(encodeItemArtwork()).toEqual(Buffer.from([0]));
    expect(() => encodeItemArtwork({ width: 95, height: 144, packed }))
      .toThrow(/dimensions/);
  });

  it('includes the media title in STREAM_INFO', () => {
    const payload = encodeStreamInfo(
      {
        width: 200,
        height: 120,
        fps: 6,
        audioSampleRate: 8_000,
        audioSampleFormat: AudioSampleFormat.Signed8,
      },
      5_432_100n,
      'A Very Jelly Episode',
      { x: 0, y: 36, width: 400, height: 168 },
    );
    const titleLength = payload.readUInt8(24);
    const viewportOffset = 25 + titleLength;

    expect(payload.readUInt16BE(0)).toBe(200);
    expect(payload.readUInt16BE(2)).toBe(120);
    expect(payload.readUInt16BE(4)).toBe(6);
    expect(payload.readUInt32BE(8)).toBe(8_000);
    expect(payload.readUInt8(13)).toBe(AudioSampleFormat.Signed8);
    expect(payload.readBigUInt64BE(16)).toBe(5_432_100n);
    expect(titleLength).toBe(20);
    expect(payload.toString('utf8', 25, viewportOffset)).toBe('A Very Jelly Episode');
    expect({
      x: payload.readUInt16BE(viewportOffset),
      y: payload.readUInt16BE(viewportOffset + 2),
      width: payload.readUInt16BE(viewportOffset + 4),
      height: payload.readUInt16BE(viewportOffset + 6),
    }).toEqual({ x: 0, y: 36, width: 400, height: 168 });
  });

  it('decodes fixed-width client playback statistics', () => {
    const payload = Buffer.alloc(28);
    [4, 7, 5_292, 2, 11, 2, 12_345].forEach((value, index) => payload.writeUInt32BE(value, index * 4));
    expect(decodeClientStats(payload)).toEqual({
      queuedVideoFrames: 4,
      droppedVideoFrames: 7,
      queuedAudioSamples: 5_292,
      audioUnderruns: 2,
      droppedAudioSamples: 11,
      appMode: 2,
      audioPlayheadMs: 12_345,
    });
  });
});
