// Temporary TS-side AIC diagnostics. No clock/row allocation on normal hosts.
// The diagnostic app owns collection and console output; these hooks only
// delimit framework work that otherwise appears as undifferentiated JS time.
function trace() {
  return (globalThis as { __pocketTsPerf?: { begin(name: string): unknown; end(span: unknown): void } }).__pocketTsPerf;
}
export function beginTsPerf(name: string): unknown { return trace()?.begin(name); }
export function endTsPerf(span: unknown): void { trace()?.end(span); }
