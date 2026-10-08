import { expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const root = new URL("..", import.meta.url).pathname;
test("AIC retains active touch snapshots and paces across the boot tick wrap", () => {
  const temporary = mkdtempSync(join(tmpdir(), "pocketjs-aic-port-"));
  try {
    const run = (args: string[]) => {
      const result = Bun.spawnSync(args, { stdout: "pipe", stderr: "pipe" });
      expect(result.exitCode, result.stdout.toString() + result.stderr.toString()).toBe(0);
      return result.stdout.toString();
    };
    const binary = join(temporary, "touch-time");
    run([process.env.CC ?? "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
      "-I" + join(root, "hosts/aic/tests/include"), "-I" + join(root, "hosts/aic/port"),
      join(root, "hosts/aic/tests/touch_time.c"), join(root, "hosts/aic/port/pocketjs_touch.c"), "-o", binary]);
    expect(run([binary])).toContain("AIC touch snapshots and tick wrap regressions passed");
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
