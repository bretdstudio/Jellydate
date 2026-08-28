export class WireBudget {
  private availableBytes: number;
  private lastClockMs: number;

  constructor(
    private readonly bytesPerSecond: number,
    private readonly capacityBytes: number,
    initialClockMs = performance.now(),
  ) {
    if (bytesPerSecond <= 0 || capacityBytes <= 0) {
      throw new Error('Wire budget rate and capacity must be positive');
    }
    this.availableBytes = capacityBytes;
    this.lastClockMs = initialClockMs;
  }

  trySpend(bytes: number, clockMs = performance.now(), force = false): boolean {
    const elapsedMs = Math.max(0, clockMs - this.lastClockMs);
    this.availableBytes = Math.min(
      this.capacityBytes,
      this.availableBytes + elapsedMs * this.bytesPerSecond / 1000,
    );
    this.lastClockMs = clockMs;
    if (!force && bytes > this.availableBytes) return false;
    this.availableBytes = Math.max(0, this.availableBytes - bytes);
    return true;
  }
}
