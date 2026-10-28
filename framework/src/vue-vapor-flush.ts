import { beginTsPerf, endTsPerf } from "./perf-trace";
// Installed by the pinned Vue runtime adapter in jsx-plugin.ts.
let flush: (() => void) | undefined;
export function installVueFrameFlush(callback: () => void): void { flush = callback; }
export function flushVueUpdates(): void {
  const span = beginTsPerf("vapor.flush");
  try { flush?.(); } finally { endTsPerf(span); }
}
