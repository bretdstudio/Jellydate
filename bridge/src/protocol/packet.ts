import {
  AudioSampleFormat,
  HEADER_SIZE,
  MAGIC,
  PacketType,
  PROTOCOL_VERSION,
} from './constants.js';

export interface Packet {
  readonly type: PacketType;
  readonly flags: number;
  readonly timestampUs: bigint;
  readonly sequence: number;
  readonly payload: Buffer;
}

export function encodePacket(packet: Packet): Buffer {
  const result = Buffer.allocUnsafe(HEADER_SIZE + packet.payload.length);
  MAGIC.copy(result, 0);
  result.writeUInt8(PROTOCOL_VERSION, 4);
  result.writeUInt8(packet.type, 5);
  result.writeUInt16BE(packet.flags, 6);
  result.writeUInt32BE(packet.payload.length, 8);
  result.writeBigUInt64BE(packet.timestampUs, 12);
  result.writeUInt32BE(packet.sequence >>> 0, 20);
  packet.payload.copy(result, HEADER_SIZE);
  return result;
}

export class PacketParser {
  private buffered: Buffer = Buffer.alloc(0);

  push(chunk: Buffer): Packet[] {
    this.buffered = this.buffered.length === 0
      ? chunk
      : Buffer.concat([this.buffered, chunk]);
    const packets: Packet[] = [];

    while (this.buffered.length >= HEADER_SIZE) {
      if (!this.buffered.subarray(0, 4).equals(MAGIC)) {
        const nextMagic = this.buffered.indexOf(MAGIC, 1);
        if (nextMagic < 0) {
          this.buffered = this.buffered.subarray(Math.max(0, this.buffered.length - 3));
          break;
        }
        this.buffered = this.buffered.subarray(nextMagic);
        continue;
      }

      const version = this.buffered.readUInt8(4);
      if (version !== PROTOCOL_VERSION) {
        throw new Error(`Unsupported Jellydate protocol version ${version}`);
      }
      const payloadLength = this.buffered.readUInt32BE(8);
      if (payloadLength > 1024 * 1024) throw new Error('Packet payload exceeds 1 MiB');
      const packetLength = HEADER_SIZE + payloadLength;
      if (this.buffered.length < packetLength) break;

      packets.push({
        type: this.buffered.readUInt8(5) as PacketType,
        flags: this.buffered.readUInt16BE(6),
        timestampUs: this.buffered.readBigUInt64BE(12),
        sequence: this.buffered.readUInt32BE(20),
        payload: Buffer.from(this.buffered.subarray(HEADER_SIZE, packetLength)),
      });
      this.buffered = this.buffered.subarray(packetLength);
    }
    return packets;
  }
}

export interface PlayCommand {
  readonly itemId: string;
  readonly startMs: bigint;
}

export interface HomeItem {
  readonly id: string;
  readonly title: string;
  readonly subtitle: string;
  readonly positionMs: bigint;
  readonly durationMs: bigint;
}

export function encodeHomeItems(items: readonly HomeItem[]): Buffer {
  const encoded = items.slice(0, 8).map((item) => {
    const id = truncateUtf8(item.id, 63);
    const title = truncateUtf8(item.title, 95);
    const subtitle = truncateUtf8(item.subtitle, 95);
    const entry = Buffer.allocUnsafe(3 + id.length + title.length + subtitle.length + 16);
    let cursor = 0;
    entry.writeUInt8(id.length, cursor++);
    id.copy(entry, cursor);
    cursor += id.length;
    entry.writeUInt8(title.length, cursor++);
    title.copy(entry, cursor);
    cursor += title.length;
    entry.writeUInt8(subtitle.length, cursor++);
    subtitle.copy(entry, cursor);
    cursor += subtitle.length;
    entry.writeBigUInt64BE(item.positionMs, cursor);
    entry.writeBigUInt64BE(item.durationMs, cursor + 8);
    return entry;
  });
  return Buffer.concat([Buffer.from([encoded.length]), ...encoded]);
}

export interface ClientStats {
  readonly queuedVideoFrames: number;
  readonly droppedVideoFrames: number;
  readonly queuedAudioSamples: number;
  readonly audioUnderruns: number;
  readonly droppedAudioSamples: number;
  readonly appMode: number;
  readonly audioPlayheadMs: number;
}

export function decodeClientStats(payload: Buffer): ClientStats {
  if (payload.length !== 28) throw new Error('CLIENT_STATS payload must be 28 bytes');
  return {
    queuedVideoFrames: payload.readUInt32BE(0),
    droppedVideoFrames: payload.readUInt32BE(4),
    queuedAudioSamples: payload.readUInt32BE(8),
    audioUnderruns: payload.readUInt32BE(12),
    droppedAudioSamples: payload.readUInt32BE(16),
    appMode: payload.readUInt32BE(20),
    audioPlayheadMs: payload.readUInt32BE(24),
  };
}

export function encodePlayCommand(command: PlayCommand): Buffer {
  const id = Buffer.from(command.itemId, 'utf8');
  if (id.length > 255) throw new Error('Item id is too long');
  const payload = Buffer.allocUnsafe(1 + id.length + 8);
  payload.writeUInt8(id.length, 0);
  id.copy(payload, 1);
  payload.writeBigUInt64BE(command.startMs, 1 + id.length);
  return payload;
}

export function decodePlayCommand(payload: Buffer): PlayCommand {
  if (payload.length < 9) throw new Error('PLAY payload is truncated');
  const idLength = payload.readUInt8(0);
  if (payload.length !== 1 + idLength + 8) throw new Error('PLAY payload has invalid length');
  return {
    itemId: payload.toString('utf8', 1, 1 + idLength),
    startMs: payload.readBigUInt64BE(1 + idLength),
  };
}

export interface StreamViewport {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

export interface StreamFormat {
  readonly width: number;
  readonly height: number;
  readonly fps: number;
  readonly audioSampleRate: number;
  readonly audioSampleFormat: AudioSampleFormat;
}

export function encodeStreamInfo(
  format: StreamFormat,
  durationMs: bigint,
  title = '',
  viewport: StreamViewport = { x: 0, y: 0, width: 400, height: 240 },
): Buffer {
  const titleBytes = truncateUtf8(title, 95);
  const viewportOffset = 25 + titleBytes.length;
  const payload = Buffer.alloc(viewportOffset + 8);
  payload.writeUInt16BE(format.width, 0);
  payload.writeUInt16BE(format.height, 2);
  payload.writeUInt16BE(format.fps, 4);
  payload.writeUInt16BE(1, 6);
  payload.writeUInt32BE(format.audioSampleRate, 8);
  payload.writeUInt8(1, 12); // channels
  payload.writeUInt8(format.audioSampleFormat, 13);
  payload.writeBigUInt64BE(durationMs, 16);
  payload.writeUInt8(titleBytes.length, 24);
  titleBytes.copy(payload, 25);
  payload.writeUInt16BE(viewport.x, viewportOffset);
  payload.writeUInt16BE(viewport.y, viewportOffset + 2);
  payload.writeUInt16BE(viewport.width, viewportOffset + 4);
  payload.writeUInt16BE(viewport.height, viewportOffset + 6);
  return payload;
}

function truncateUtf8(value: string, maxBytes: number): Buffer {
  const encoded = Buffer.from(value, 'utf8');
  if (encoded.length <= maxBytes) return encoded;
  return Buffer.from(encoded.subarray(0, maxBytes).toString('utf8').replace(/\uFFFD$/, ''), 'utf8');
}
