import { expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const root = new URL("..", import.meta.url).pathname;
test("AIC decodes device SYSMAP readbacks using E907 attribute bit positions", () => {
  const temporary = mkdtempSync(join(tmpdir(), "pocketjs-aic-hw-"));
  try {
    const binary = join(temporary, "hw-decode");
    const built = Bun.spawnSync([process.env.CC ?? "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
      "-I" + join(root, "hosts/aic/port"), join(root, "hosts/aic/tests/hw_decode.c"), "-o", binary],
      { stdout: "pipe", stderr: "pipe" });
    expect(built.exitCode, built.stderr.toString()).toBe(0);
    const ran = Bun.spawnSync([binary], { stdout: "pipe", stderr: "pipe" });
    expect(ran.exitCode, ran.stderr.toString()).toBe(0);
    expect(ran.stdout.toString()).toContain("AIC hardware attribute decoding passed");
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});

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

test("AIC GE accelerator maps renderer ops onto mpp_ge with cache ordering", () => {
  const temporary = mkdtempSync(join(tmpdir(), "pocketjs-aic-port-"));
  try {
    const run = (args: string[]) => {
      const result = Bun.spawnSync(args, { stdout: "pipe", stderr: "pipe" });
      expect(result.exitCode, result.stdout.toString() + result.stderr.toString()).toBe(0);
      return result.stdout.toString();
    };
    const cflags = ["-std=c11", "-Wall", "-Wextra", "-Werror",
      "-I" + join(root, "hosts/aic/tests/include/ge"),
      "-I" + join(root, "hosts/aic/tests/include"),
      "-I" + join(root, "hosts/aic/port"),
      "-I" + join(root, "hosts/esp-idf/components/pocketjs_render_rgb565/include")];
    const binary = join(temporary, "ge-accel");
    run([process.env.CC ?? "cc", ...cflags,
      join(root, "hosts/aic/tests/ge_accel.c"), join(root, "hosts/aic/port/pocketjs_ge.c"), "-o", binary]);
    expect(run([binary])).toContain("AIC GE accelerator regressions passed");

    const disabled = join(temporary, "ge-disabled");
    run([process.env.CC ?? "cc", ...cflags, "-DPOCKETJS_AIC_GE_DISABLE",
      join(root, "hosts/aic/tests/ge_disabled.c"), join(root, "hosts/aic/port/pocketjs_ge.c"), "-o", disabled]);
    expect(run([disabled])).toContain("AIC GE compiled-out regressions passed");
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
