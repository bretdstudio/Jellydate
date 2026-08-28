const HEADER_BYTES = 4;
const MAX_COALESCED_GAP = 3;
const REPEAT_FLAG = 0x8000;
const LENGTH_MASK = 0x7fff;
/* Splitting a literal into literal/repeat/literal adds two record headers.
   Require ten equal bytes so the repeat token always produces a net saving. */
const MIN_REPEAT_LENGTH = 10;

/**
 * Encodes changed byte ranges relative to the preceding packed 1-bit frame.
 * Each record is: u16 unchanged-byte skip, u16 literal length, literal bytes.
 * A trailing unchanged region is implicit. Empty payload means an identical frame.
 */
export function encodeFrameDelta(previous: Buffer, next: Buffer, repeatRuns = true): Buffer {
  if (previous.length !== next.length || next.length > 0xffff) {
    throw new Error('Delta frames must have equal lengths no larger than 65,535 bytes');
  }

  const records: Buffer[] = [];
  let cursor = 0;
  while (cursor < next.length) {
    let literalStart = cursor;
    while (literalStart < next.length && previous[literalStart] === next[literalStart]) {
      literalStart += 1;
    }
    if (literalStart === next.length) break;

    let literalEnd = literalStart + 1;
    while (literalEnd < next.length) {
      if (previous[literalEnd] !== next[literalEnd]) {
        literalEnd += 1;
        continue;
      }
      const gapStart = literalEnd;
      while (literalEnd < next.length &&
             previous[literalEnd] === next[literalEnd] &&
             literalEnd - gapStart <= MAX_COALESCED_GAP) {
        literalEnd += 1;
      }
      if (literalEnd - gapStart > MAX_COALESCED_GAP || literalEnd === next.length) {
        literalEnd = gapStart;
        break;
      }
    }

    let tokenStart = literalStart;
    while (tokenStart < literalEnd) {
      const repeated = repeatLength(next, tokenStart, literalEnd);
      if (repeatRuns && repeated >= MIN_REPEAT_LENGTH) {
        const record = Buffer.allocUnsafe(HEADER_BYTES + 1);
        record.writeUInt16BE(tokenStart - cursor, 0);
        record.writeUInt16BE(REPEAT_FLAG | repeated, 2);
        record[HEADER_BYTES] = next[tokenStart] ?? 0;
        records.push(record);
        cursor = tokenStart + repeated;
        tokenStart = cursor;
        continue;
      }

      let tokenEnd = tokenStart + repeated;
      while (tokenEnd < literalEnd && tokenEnd - tokenStart < LENGTH_MASK) {
        const nextRepeat = repeatLength(next, tokenEnd, literalEnd);
        if (repeatRuns && nextRepeat >= MIN_REPEAT_LENGTH) break;
        tokenEnd += nextRepeat;
      }
      const literalLength = tokenEnd - tokenStart;
      const record = Buffer.allocUnsafe(HEADER_BYTES + literalLength);
      record.writeUInt16BE(tokenStart - cursor, 0);
      record.writeUInt16BE(literalLength, 2);
      next.copy(record, HEADER_BYTES, tokenStart, tokenEnd);
      records.push(record);
      cursor = tokenEnd;
      tokenStart = tokenEnd;
    }
    cursor = literalEnd;
  }
  return Buffer.concat(records);
}

export function applyFrameDelta(frame: Buffer, delta: Buffer): void {
  let frameCursor = 0;
  let deltaCursor = 0;
  while (deltaCursor < delta.length) {
    if (delta.length - deltaCursor < HEADER_BYTES) throw new Error('Truncated delta record');
    const skipLength = delta.readUInt16BE(deltaCursor);
    const control = delta.readUInt16BE(deltaCursor + 2);
    const repeated = (control & REPEAT_FLAG) !== 0;
    const literalLength = control & LENGTH_MASK;
    deltaCursor += HEADER_BYTES;
    frameCursor += skipLength;
    if (literalLength === 0 || frameCursor + literalLength > frame.length ||
        deltaCursor + (repeated ? 1 : literalLength) > delta.length) {
      throw new Error('Invalid delta record');
    }
    if (repeated) {
      frame.fill(delta[deltaCursor] ?? 0, frameCursor, frameCursor + literalLength);
      deltaCursor += 1;
    } else {
      delta.copy(frame, frameCursor, deltaCursor, deltaCursor + literalLength);
      deltaCursor += literalLength;
    }
    frameCursor += literalLength;
  }
}

function repeatLength(bytes: Buffer, start: number, end: number): number {
  const value = bytes[start];
  let cursor = start + 1;
  const limit = Math.min(end, start + LENGTH_MASK);
  while (cursor < limit && bytes[cursor] === value) cursor += 1;
  return cursor - start;
}
