export const MAGIC = Buffer.from('JDAT', 'ascii');
export const PROTOCOL_VERSION = 1;
export const HEADER_SIZE = 24;

export const SCREEN_WIDTH = 400;
export const SCREEN_HEIGHT = 240;
export const PACKED_ROW_BYTES = SCREEN_WIDTH / 8;
export const PACKED_FRAME_BYTES = PACKED_ROW_BYTES * SCREEN_HEIGHT;
export const DETAIL_ARTWORK_WIDTH = 96;
export const DETAIL_ARTWORK_HEIGHT = 144;
export const DETAIL_ARTWORK_BYTES = DETAIL_ARTWORK_WIDTH / 8 * DETAIL_ARTWORK_HEIGHT;

export enum AudioSampleFormat {
  Signed16LittleEndian = 1,
  Signed8 = 2,
}

export enum PacketType {
  Hello = 0x01,
  Auth = 0x02,
  Play = 0x03,
  Pause = 0x04,
  Resume = 0x05,
  Seek = 0x06,
  Stop = 0x07,
  VideoKeyframe = 0x10,
  VideoDelta = 0x11,
  Audio = 0x12,
  PlaybackState = 0x13,
  BufferState = 0x14,
  EndOfStream = 0x15,
  Error = 0x16,
  Ping = 0x17,
  Pong = 0x18,
  StreamInfo = 0x19,
  ClientStats = 0x1a,
  KeyframeRequest = 0x1b,
  HomeRequest = 0x20,
  HomeResponse = 0x21,
  ItemDetailsRequest = 0x22,
  ItemDetailsResponse = 0x23,
  ItemArtworkRequest = 0x24,
  ItemArtworkResponse = 0x25,
}

export enum PacketFlags {
  None = 0,
  Discontinuity = 1 << 0,
}
