# Jellydate architecture

Research baseline: **August 25, 2026**.

## Current technical findings

### Playdate

- The current public SDK is **3.1.1** (July 22, 2026). TCP and HTTP arrived in OS 2.7 and remain the supported networking path. The device permits up to four simultaneous connections. C clients must explicitly request network permission before creating a connection. Sources: [SDK changelog](https://sdk.play.date/changelog/), [Inside Playdate with C 3.1.1](https://sdk.play.date/3.1.1/Inside%20Playdate%20with%20C.html).
- TCP reads and writes can be partial. `getBytesAvailable()` is a hint, not a packet boundary, so Jellydate has a stateful streaming parser and a queued write path.
- Client network work is one bounded 4 KiB read per display update. The hardware read timeout is applied after `open()` so the SDK cannot reset it during connection setup; requesting a full chunk also avoids a firmware watchdog stall on tiny TCP fragments. Complete video packets are reconstructed by the streaming parser and only the newest eligible frame is presented.
- The 3.1.1 HTML documents newer permission/connection error enum values that are not present in the 3.1.1 macOS SDK header. The client therefore treats denial/creation failure as a generic closed connection rather than depending on those newer symbolic values.
- `graphics->getFrame()` returns a framebuffer with **52-byte row stride**. Only 50 bytes represent 400 pixels; the other two bytes per row are padding. Direct writes require `markUpdatedRows()`. The client accepts 12,000-byte 400×240, 4,320-byte 240×144, or 3,000-byte 200×120 frames. Precomputed nearest-neighbor maps scale negotiated intermediate sizes while copying into the padded framebuffer.
- The sound engine runs at **44,100 frames/second**. Network code fills a fixed ring buffer in the update loop; the allocation-free `sound->addSource()` callback only consumes samples. The client expands signed 8-bit or signed 16-bit mono PCM and resamples the negotiated input rate to the hardware clock, with silence-on-underrun and a 200 ms startup/recovery threshold.
- Video is held in an eight-frame fixed queue. Playback starts after at least six video frames and 200 ms of audio are ready; the audio playhead then selects eligible video frames. The hardware profile is fixed at 5 FPS, with temporal hysteresis and literal/repeat delta records keeping native-resolution traffic beneath the transport ceiling.
- The C API exposes crank angle change but not Lua's accelerated-change value. Jellydate derives acceleration from degrees moved per update and commits a seek after 500 ms of inactivity.
- The Playdate has 16 MB RAM. The eight-frame video queue costs 96 KB. The current three-second 44.1 kHz output-sample ring costs 256 KiB. Both are modest, but the implementation keeps explicit fixed bounds.

### Jellyfin

- `@jellyfin/sdk` **0.13.0** targets Jellyfin **10.11.x** and is used for authentication, item queries, images, playback info, and reporting. Source: [official TypeScript SDK](https://typescript-sdk.jellyfin.org/).
- Jellyfin 10.11 deprecates the old `OnPlayback*` operations. Jellydate calls `ReportPlaybackStart`, `ReportPlaybackProgress`, and `ReportPlaybackStopped`. Servers can return `503` with `Retry-After` during lifecycle transitions; retry handling remains a near-term bridge task. Source: [Jellyfin 10.11 client-development changes](https://jellyfin.org/posts/jellyfin-release-10.11.0/).
- The bridge owns Jellyfin credentials. The Playdate only knows a separate bridge token. Authenticated video data is requested with the SDK's Axios instance and piped into FFmpeg, keeping Jellyfin tokens out of process arguments and ordinary logs.

### FFmpeg

- FFmpeg's rawvideo output is appropriate because frame dimensions and format are fixed out of band. Source: [FFmpeg formats documentation](https://ffmpeg.org/ffmpeg-formats.html#rawvideo).
- The tested pipeline decodes, samples at up to 30 FPS, Lanczos-scales within the negotiated 400×240, 240×144, or 200×120 canvas, letterboxes, applies modest contrast/gamma tuning, and emits 8-bit grayscale. Node then applies a stable 8×8 ordered Bayer threshold and packs MSB-first pixels.
- The repository test exercises this exact pipeline against FFmpeg 8.1.1. Dithering in TypeScript keeps bit order explicit and testable; it is about 2.88 million threshold operations per second at 30 FPS.

## Vertical-slice data flow

```text
Jellyfin /Videos/{id}/stream
        │ authenticated Readable stream
        ▼
FFmpeg stdin ─┬→ 400×240 gray8 frames → stable 8×8 dither → keyframe/compressed-delta selection at 5 FPS
             └→ 8,000 Hz mono s8 PCM → 40 ms audio chunks (8 KB/s)
        │
        ▼
timestamped JDAT packets over authenticated TCP
        │ partial reads are expected
        ▼
Playdate packet parser ─┬→ delta reconstruction → 52-byte framebuffer rows
                       └→ s8 expansion/resampling → bounded PCM ring → 44.1 kHz callback
```

The native-resolution physical profile uses periodic 12 KB keyframes plus changed-byte deltas whose repeated byte runs receive compact tokens. With a dithering hysteresis of 16 it holds a fixed 5 FPS without the adaptive frame governor. A two-minute hardware soak sustained 5.00 FPS at about 26.2 KiB/s including 8.6 KiB/s audio and control traffic, with a zero-byte final TCP send queue. The 240×144 and 200×120 full-frame profiles remain useful fallbacks.

HTTP is reserved for low-rate discovery and artwork. TCP carries commands and continuous media. The Bridge returns compact item records instead of forwarding large Jellyfin DTOs.

## Repository boundaries

```text
bridge/src/jellyfin/      Jellyfin SDK boundary and compact DTO mapping
bridge/src/transcoder/    FFmpeg and ordered dithering
bridge/src/protocol/      shared wire concepts, TypeScript implementation
bridge/src/streaming/     TCP lifecycle and playback reporting
bridge/src/images/        1-bit poster conversion/cache
playdate/src/network.*    Playdate permission, socket, partial I/O
playdate/src/protocol.*   allocation-free streaming parser
playdate/src/video.*      padded-framebuffer boundary
playdate/src/audio.*      fixed PCM ring and real-time callback boundary
playdate/src/controls.*   A/B/crank behavior
playdate/src/ui.*         intentionally tiny pocket-TV presentation
```

## Decision log

1. **Keyframes before deltas.** The initial vertical slice used only complete frames. Native-resolution hardware mode now sends changed byte ranges between 30-second recovery keyframes; a delta is used only when smaller than the corresponding full frame, and every seek/reconnect starts with a keyframe.
2. **Ordered 8×8 Bayer dithering.** It is deterministic, temporally stable, and cheap. Error diffusion can look excellent in still images but tends to crawl between moving frames.
3. **Audio is the master clock.** PCM and video land with presentation timestamps. The client holds future video, selects the newest eligible frame from the audio playhead, and drops late frames; continuous audio beats perfect frame delivery.
4. **No server token on Playdate.** Compromise of the handheld token only reaches the narrow bridge surface.
5. **One active stream per TCP connection.** A seek tears down FFmpeg and starts a new stream with a discontinuity flag and immediate full frame.
6. **Bound audio lead at the Bridge.** Independent FFmpeg pipes can become uneven even when both average real time. Audio is held to at most 120 ms ahead of the last transmitted video timestamp so it cannot fill the socket and make video arrive in bursts.
7. **Pace presentation in Jellydate, not FFmpeg.** FFmpeg real-time input mode tried to catch up after pipe pressure, producing visible frame clusters despite a correct average rate. The Bridge now schedules frames against a monotonic clock and exposes maximum frame-gap telemetry.

## Next implementation steps

1. Extend the physical-hardware endurance gate from the initial real-time stream to a full movie.
2. Add Jellyfin lifecycle retry handling and reconnect/keyframe recovery.
3. Add compact media details, then add persistent keyboard-based bridge setup.
4. Characterize the fixed 5 FPS compressed-delta profile across broader movie content and Wi-Fi conditions; retain the adaptive wire budget as a fallback for unusually weak links.

## Reliability telemetry

The bridge exposes authenticated live playback telemetry at `/api/telemetry`.
It keeps only the current session's bounded counters: frame and byte throughput,
socket backpressure, seek/pause/resume activity, Jellyfin lifecycle report health,
and current process memory. `npm run soak` samples that endpoint and produces a
pass/fail JSON report without changing playback or retaining media data.
