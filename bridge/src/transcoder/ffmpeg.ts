import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import type { Readable } from 'node:stream';

export interface FfmpegInput {
  readonly path?: string;
  readonly stream?: Readable;
}

export interface RawVideoProcess {
  readonly process: ChildProcessWithoutNullStreams;
  readonly frames: Readable;
  readonly audio: Readable;
}

export interface RawMediaProfile {
  readonly fps: number;
  readonly videoWidth: number;
  readonly videoHeight: number;
  readonly audioSampleRate: number;
  readonly audioSampleFormat: 's8' | 's16le';
}

export function spawnRawVideo(
  ffmpegPath: string,
  input: FfmpegInput,
  profile: RawMediaProfile,
  startMs: bigint,
): RawVideoProcess {
  if (Boolean(input.path) === Boolean(input.stream)) {
    throw new Error('FFmpeg input must contain exactly one of path or stream');
  }

  const filter = [
    `fps=${profile.fps}`,
    `scale=${profile.videoWidth}:${profile.videoHeight}:force_original_aspect_ratio=decrease:flags=lanczos`,
    `pad=${profile.videoWidth}:${profile.videoHeight}:(ow-iw)/2:(oh-ih)/2:black`,
    'eq=contrast=1.10:gamma=0.95',
    'format=gray',
  ].join(',');

  const args = [
    '-hide_banner', '-loglevel', 'warning',
    ...(startMs > 0n ? ['-ss', (Number(startMs) / 1000).toFixed(3)] : []),
    '-i', input.path ?? 'pipe:0',
    '-sn', '-dn',
    '-map', '0:v:0', '-an',
    '-vf', filter,
    '-f', 'rawvideo', '-pix_fmt', 'gray', 'pipe:1',
    '-map', '0:a:0?', '-vn',
    '-ac', '1', '-ar', String(profile.audioSampleRate),
    '-c:a', profile.audioSampleFormat === 's8' ? 'pcm_s8' : 'pcm_s16le',
    '-f', profile.audioSampleFormat, 'pipe:3',
  ];
  const child = spawn(ffmpegPath, args, { stdio: ['pipe', 'pipe', 'pipe', 'pipe'] });
  const audio = child.stdio[3] as Readable;
  if (input.stream) {
    // Either side may close first during stop/seek or when FFmpeg rejects an
    // input. Both streams need error handlers or Node treats EPIPE as fatal.
    child.stdin.on('error', (error: NodeJS.ErrnoException) => {
      if (error.code !== 'EPIPE') child.emit('error', error);
    });
    input.stream.on('error', (error: NodeJS.ErrnoException) => {
      if (error.code !== 'ECONNRESET' && error.code !== 'EPIPE') child.emit('error', error);
      child.stdin.destroy();
    });
    child.once('close', () => {
      input.stream?.unpipe(child.stdin);
      input.stream?.destroy();
    });
    input.stream.pipe(child.stdin);
  } else {
    child.stdin.end();
  }
  return {
    process: child as ChildProcessWithoutNullStreams,
    frames: child.stdout,
    audio,
  };
}
