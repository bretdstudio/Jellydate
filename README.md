# Jellydate

Jellydate is a Jellyfin client for Playdate, because apparently watching movies at 400×240 in one-bit black and white is something that needed to exist.

This repository contains two deliberately unequal halves:

- `bridge/` authenticates with Jellyfin, runs FFmpeg, dithers each frame, and serves a tiny HTTP API plus a binary TCP stream.
- `playdate/` receives almost-display-ready bytes, copies them into the framebuffer, and responds to A, B, and the crank.

The current `0.1` spike proves the end-to-end media path in both the Simulator and on physical Playdate hardware. It has real Jellyfin browsing endpoints, authenticated media input, negotiated ordered-dithered video and mono PCM audio, audio-master A/V synchronization, pause/stop, crank seeking, and playback lifecycle reporting. A Playdate-native library browser and persistent setup are the next milestones.

## First transmission received

On August 25, 2026, Jellydate streamed its first real Jellyfin video into the Playdate Simulator: moving, letterboxed, beautifully ridiculous 1-bit frames. On August 27, timestamped mono audio joined the transmission. On August 28, the physical Playdate streamed the hardware profile in real time with audible audio, pause/resume, and crank seeking.

## Quick start: bridge

Requirements: Node.js 22+, FFmpeg, and a Jellyfin 10.11.x server.

```bash
cd bridge
cp .env.example .env
npm install
npm run dev
```

Fill in the Jellyfin values and use a long random `JELLYDATE_TOKEN`. The default bind address is loopback-only. For a physical Playdate, set `BRIDGE_HOST=0.0.0.0`, keep the bridge on a trusted LAN, and allow TCP ports 7789 and 7790 in the host firewall.

Check the bridge and find a media id:

```bash
curl http://127.0.0.1:7789/health
curl -H 'x-jellydate-token: YOUR_TOKEN' http://127.0.0.1:7789/api/home
```

For a local-file transcoder smoke test, set `TEST_MEDIA_PATH` to an absolute media path and leave the Playdate item id as `__test__`. The bridge still authenticates to Jellyfin at startup, but that test stream bypasses Jellyfin media delivery.

## Quick start: Playdate

Install the current [Playdate SDK](https://play.date/dev/), then generate the gitignored device configuration:

```bash
cd bridge
npm run generate-playdate-config -- YOUR_BRIDGE_LAN_IP
```

This reads the bridge token from `.env` without copying it into tracked source. Set the desired item id in the generated `playdate/src/config_private.h`, or keep `__test__` for `TEST_MEDIA_PATH`.

Then:

```bash
cd playdate
make
make simulator
```

On first connection, Playdate asks permission to reach the bridge. A pauses/resumes, B stops, and the crank scrubs. Slow crank motion moves by seconds; faster motion winds the imaginary reel increasingly quickly. The seek is sent half a second after crank movement stops.

## Useful development commands

```bash
cd bridge
npm run check

cd ..
mkdir -p /tmp/jellydate-tests
clang -std=c11 -Wall -Wextra -Werror -Iplaydate/src \
  playdate/src/protocol.c playdate/test/protocol_test.c \
  -o /tmp/jellydate-tests/protocol_test
/tmp/jellydate-tests/protocol_test

clang -std=c11 -Wall -Wextra -Werror \
  -DTARGET_SIMULATOR=1 -DTARGET_EXTENSION=1 \
  -I"$PLAYDATE_SDK_PATH/C_API" -Iplaydate/src \
  playdate/src/video.c playdate/test/video_test.c \
  -o /tmp/jellydate-tests/video_test
/tmp/jellydate-tests/video_test
```

## Playback soak test

The authenticated telemetry endpoint reports live video and audio throughput, client queue depth, video/audio drops, audio underruns, wire bytes,
socket backpressure, FFmpeg restarts, Jellyfin progress-report health, and bridge
memory without exposing credentials. With the bridge and Simulator already
playing, run:

```bash
cd bridge
npm run soak -- --minutes 30
```

The watchdog fails on a terminal stream state, a frame stall longer than 20
seconds, or bridge RSS growth above 128 MiB. It writes the full sample series and
summary to `bridge/soak-results/`. Use `--seconds 30` for a quick smoke run and
`--minutes 180` for the full-movie endurance gate.

Start with [`ARCHITECTURE.md`](ARCHITECTURE.md), then read [`PROTOCOL.md`](PROTOCOL.md) before changing either stream implementation. Hardware observations belong in [`HARDWARE_TESTS.md`](HARDWARE_TESTS.md).

## Deliberate limitations

This is the shortest path to real Jellyfin playback on Playdate. Physical hardware currently receives native 400×240 one-bit video at a fixed 5 FPS as periodic full keyframes plus compact literal/repeat changed-byte deltas, alongside 8 kHz signed 8-bit audio. Temporal dithering hysteresis reduces noisy pixel churn without lowering resolution. The physical two-minute soak held 5.00 FPS at about 26.2 KiB/s with zero backpressure, media drops, or underruns and an empty final TCP send queue. The adaptive wire governor and simpler 240×144 or 200×120 profiles remain available for weaker Wi-Fi. Audio is the presentation clock for a bounded timestamped video queue.

## Security

The HTTP API and TCP stream require `JELLYDATE_TOKEN`; `/health` is the only unauthenticated route. The bridge never logs configured passwords or tokens, and it pipes authenticated Jellyfin responses into FFmpeg so the Jellyfin token does not appear in FFmpeg's process arguments. The bridge binds to `127.0.0.1` unless explicitly changed. This is still a personal-LAN experiment—do not expose it directly to the internet.
