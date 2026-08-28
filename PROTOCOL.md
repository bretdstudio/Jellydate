# Jellydate Stream Protocol (JDSP) v1

JDSP is a small, big-endian, length-prefixed protocol over one TCP connection. TCP is a byte stream: one read may contain half a header, half a payload, one packet, or several packets. Both implementations preserve parser state across reads.

## Header

Every packet starts with 24 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `JDAT` |
| 4 | 1 | version | `1` |
| 5 | 1 | type | packet type below |
| 6 | 2 | flags | bit field |
| 8 | 4 | payload length | unsigned bytes; maximum 1 MiB on Bridge, 12,000 on the current client |
| 12 | 8 | timestamp | presentation time in microseconds; zero for untimed control packets |
| 20 | 4 | sequence | unsigned packet sequence, wrapping naturally |

All integers are unsigned and big-endian. Video pixels inside a byte are MSB-first: the leftmost pixel is bit `0x80`. A set bit is white.

## Types

| Hex | Name | Direction | Payload |
|---:|---|---|---|
| `01` | `HELLO` | Bridge → client | UTF-8 implementation label |
| `02` | `AUTH` | client → Bridge | UTF-8 bridge token |
| `03` | `PLAY` | client → Bridge | binary play command |
| `04` | `PAUSE` | client → Bridge | empty |
| `05` | `RESUME` | client → Bridge | empty |
| `06` | `SEEK` | client → Bridge | target milliseconds, u64 |
| `07` | `STOP` | client → Bridge | empty |
| `10` | `VIDEO_KEYFRAME` | Bridge → client | packed 1-bit bitmap, exactly `(width / 8) × height` bytes |
| `11` | `VIDEO_DELTA` | Bridge → client | changed byte ranges relative to the preceding decoded video frame |
| `12` | `AUDIO` | Bridge → client | mono PCM in the format declared by `STREAM_INFO` |
| `13` | `PLAYBACK_STATE` | Bridge → client | short UTF-8 state |
| `14` | `BUFFER_STATE` | Bridge → client | reserved |
| `15` | `END_OF_STREAM` | Bridge → client | empty |
| `16` | `ERROR` | either | short human-readable UTF-8 detail |
| `17` | `PING` | either | opaque echo bytes |
| `18` | `PONG` | either | exact PING payload and timestamp |
| `19` | `STREAM_INFO` | Bridge → client | Binary stream descriptor and media title |
| `1A` | `CLIENT_STATS` | client → Bridge | five u32 buffer/drop counters followed by app mode and audio playhead milliseconds |
| `20` | `HOME_REQUEST` | client → Bridge | catalog selector plus optional parent id (`0` Continue Watching, `1` Movies, `2` TV series, `3` Recently Added, `4` seasons, `5` episodes) |
| `21` | `HOME_RESPONSE` | Bridge → client | up to eight entries from the requested catalog with resume metadata |
| `22` | `ITEM_DETAILS_REQUEST` | client → Bridge | request compact metadata for one Jellyfin item id |
| `23` | `ITEM_DETAILS_RESPONSE` | Bridge → client | title, context, overview, runtime, and resume position |

Flag bit 0 (`DISCONTINUITY`) means buffered media from the prior timeline must be discarded. It is set on `STREAM_INFO` after play/seek and on the first following keyframe.

### HOME_RESPONSE payload

`HOME_REQUEST` begins with `u8 catalog_kind`, followed by `u8 parent_id_length + parent_id`, then a big-endian `u16 start_index`. Continue Watching and Recently Added use a zero-length parent id. Movie and TV-series requests use a single `A`–`Z` bucket or `#` for titles sorted before A; seasons require a series id and episodes require a season id. Every catalog advances the index in eight-item pages. Legacy requests that omit the index remain accepted and start at zero.

`HOME_RESPONSE` begins with `u8 item_count` and `u8 catalog_flags`; flag bit 0 means another page exists. Each item then contains three length-prefixed UTF-8 fields—`u8 id_length + id`, `u8 title_length + title`, and `u8 subtitle_length + subtitle`—followed by big-endian `u64 position_ms` and `u64 duration_ms`. The bridge returns at most eight entries. Continue Watching preserves Jellyfin resume positions; Movies and TV series are alphabetized within the requested letter bucket; TV drill-down returns seasons and then playable episodes; and Recently Added mixes new movies and episodes. Additional pages load automatically when navigation crosses a page boundary. All playable entries include any saved position available from Jellyfin user data.

### ITEM_DETAILS payloads

`ITEM_DETAILS_REQUEST` contains `u8 item_id_length + item_id`. `ITEM_DETAILS_RESPONSE` contains `u8 title_length + title`, `u8 subtitle_length + subtitle`, `u16 overview_length + overview`, then big-endian `u64 position_ms` and `u64 duration_ms`. The subtitle provides compact media context such as movie type and year or series, season, and episode number. Playdate uses the saved position to label the primary action `RESUME` and begin playback at that point.

### VIDEO_DELTA payload

Each delta contains zero or more records. A record begins with `u16 unchanged_byte_skip` and a big-endian `u16 control`. If control bit 15 is clear, bits 0–14 are the literal length and that many replacement bytes follow. If bit 15 is set, bits 0–14 are the repeat length and one replacement byte follows; the decoder fills the range with that byte. The decoder advances over unchanged bytes, applies the replacement to its preceding decoded packed frame, and treats any unmentioned trailing bytes as unchanged. Length zero is invalid. An empty payload repeats the preceding frame.

The first frame after `STREAM_INFO` is always a full keyframe. The current high-fidelity profile sends a periodic recovery keyframe every 30 seconds and falls back to a keyframe whenever a delta would be no smaller than the complete packed frame. Seeks and reconnects always begin a new stream with an immediate keyframe. Because deltas depend on the preceding decoded video frame, clients decode every received video packet before applying presentation-queue drops.

## PLAY payload

```text
u8    item_id_length
bytes item_id_utf8[item_id_length]
u64   start_milliseconds
```

Item IDs are opaque. `__test__` selects `TEST_MEDIA_PATH` when the bridge has that developer option configured.

## STREAM_INFO payload

| Offset | Size | Field | v1 value |
|---:|---:|---|---|
| 0 | 2 | width | negotiated; currently 200, 240, or 400 |
| 2 | 2 | height | negotiated; currently 120, 144, or 240 |
| 4 | 2 | FPS numerator | configurable; balanced hardware profile uses 5 |
| 6 | 2 | FPS denominator | 1 |
| 8 | 4 | audio sample rate | 8,000–22,050; hardware profile uses 8,000 |
| 12 | 1 | audio channels | 1 |
| 13 | 1 | audio format | 1 = signed 16-bit little-endian PCM; 2 = signed 8-bit PCM |
| 14 | 2 | reserved | zero |
| 16 | 8 | duration | milliseconds, or zero if unknown |
| 24 | 1 | title length | UTF-8 byte count, maximum 95 |
| 25 | variable | title | UTF-8 media title |
| `25 + title length` | 2 | viewport x | left edge of the fitted video |
| `27 + title length` | 2 | viewport y | top edge of the fitted video |
| `29 + title length` | 2 | viewport width | fitted video width |
| `31 + title length` | 2 | viewport height | fitted video height |

Each `AUDIO` packet contains whole mono samples. Its timestamp is the presentation time of the first sample. Packets cover 40 ms: the hardware profile sends 320 signed 8-bit samples (320 payload bytes) at 8,000 Hz, while the full profile sends 882 signed 16-bit samples (1,764 payload bytes) at 22,050 Hz. The client expands signed 8-bit input to 16-bit and resamples either source rate to Playdate's 44,100 Hz sound clock with an integer phase accumulator.

The high-fidelity physical profile sends native 400×240 packed frames (12,000 bytes before delta encoding). The 240×144 fallback uses 4,320-byte frames and a precomputed nearest-neighbor map; the lower-bandwidth 200×120 fallback uses 3,000-byte frames and expands each source pixel to a 2×2 display block.
Clients that only understand the original 24-byte descriptor may ignore the appended title and viewport fields.

## Connection flow

```text
Bridge                         Playdate
   │──── HELLO ──────────────────▶│
   │◀─── AUTH(token) ─────────────│
   │──── PLAYBACK_STATE ready ───▶│
   │◀─── PLAY(item, start) ───────│
   │──── STREAM_INFO + DISC. ────▶│
   │──── VIDEO_KEYFRAME + DISC. ─▶│
   │──── AUDIO + DISC. ─────────▶│
   │──── VIDEO / AUDIO... ───────▶│
```

Any non-PING command before successful authentication is rejected. A bad token returns `ERROR` and closes the socket.

`CLIENT_STATS` is a 28-byte diagnostic snapshot sent once per second: queued video frames, cumulative dropped video frames, queued 44.1 kHz output samples, cumulative audio underruns, cumulative dropped input samples, numeric client mode, and audio playhead milliseconds. All fields are unsigned 32-bit big-endian integers.

## Recovery rules

- Unknown magic, unsupported version, excessive payload, or malformed command is a protocol error.
- A seek replaces the active transcode, sends discontinuities, flushes client media buffers, and starts with a full keyframe and a new audio timeline.
- Disconnect stops FFmpeg and reports the latest known position to Jellyfin.
- Deltas never cross a discontinuity and are bounded by periodic keyframes. A malformed delta is a fatal stream error rather than an opportunity to display a corrupted reference chain.
