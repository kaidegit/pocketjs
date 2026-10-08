import { describe, expect, test } from "bun:test";
import {
  POCKET_AIC_HOST_SCHEMA_ID,
  generatePocketAicHostSchema,
  type PocketAicHostProfile,
} from "../contracts/spec/aic-host.ts";
import {
  aicHostBuildEnvironment,
  hashPocketAicHostProfile,
  pocketAicHostExtension,
  pocketAicHostRegistry,
  readAicHostExtension,
  validatePocketAicHostProfile,
} from "../framework/src/manifest/aic-host.ts";
import { readIdfHostExtension } from "../framework/src/manifest/idf-host.ts";
import { validateAndResolveBuildPlan } from "../framework/src/manifest/resolve.ts";
import { createHostExtension, isHostExtension } from "../framework/src/manifest/host-extension.ts";

const profile: PocketAicHostProfile = {
  $schema: POCKET_AIC_HOST_SCHEMA_ID,
  version: 1,
  id: "aic-d12x-demo68",
  platform: "aic",
  form: "takeover",
  tickHz: 60,
  display: {
    physicalViewport: [480, 272],
    logicalViewports: [[480, 272]],
    presentations: ["native"],
    rasterDensity: 1,
  },
  capabilities: ["input.touch", "text.glyphs.baked"],
};

describe("ArtInChip host profile", () => {
  test("extension identity is generic and payload validation belongs to the AIC adapter", () => {
    const other = createHostExtension("another-host", 2, { answer: 42 });
    expect(isHostExtension(other)).toBe(true);
    expect(readAicHostExtension(other)).toBeUndefined();
    const extension = pocketAicHostExtension(`sha256:${"12".repeat(32)}`, 60);
    expect(isHostExtension({ ...extension, payload: { ...extension.payload, tickHz: 61 } })).toBe(false);
    expect(() => readAicHostExtension({ ...extension, version: 2 })).toThrow(/payload\/version/);
    expect(() => readAicHostExtension(createHostExtension("aic", 1, { profileHash: "bad", tickHz: 60 }))).toThrow();
    expect(aicHostBuildEnvironment(extension).POCKETJS_TICK_HZ).toBe("60");
    expect(readIdfHostExtension(extension)).toBeUndefined();
  });
  test("committed schema matches the TypeScript source", async () => {
    const committed = await Bun.file(
      new URL("../contracts/schema/pocket-aic-host-1.json", import.meta.url),
    ).text();
    expect(committed).toBe(generatePocketAicHostSchema());
  });

  test("validates, hashes, and resolves as one project-provided target", async () => {
    const validated = validatePocketAicHostProfile(profile);
    expect(validated.ok).toBe(true);
    if (!validated.ok) return;
    const profileHash = hashPocketAicHostProfile(validated.value);
    expect(profileHash).toMatch(/^sha256:[0-9a-f]{64}$/);
    const manifest = {
      $schema: "https://pocketjs.dev/schema/pocket-2.json",
      pocket: 2,
      id: "dev.pocket-stack.aic-demo",
      name: "aic-demo",
      title: "AIC Demo",
      version: "0.1.0",
      engine: { capabilities: { requires: ["input.touch", "text.glyphs.baked"] } },
      app: {
        entry: "app/main.tsx",
        framework: "solid",
        viewport: { logical: [480, 272], presentation: "native" },
      },
    };
    const result = validateAndResolveBuildPlan(
      manifest,
      { target: profile.id, hostExtension: pocketAicHostExtension(profileHash, profile.tickHz) },
      pocketAicHostRegistry(profile),
    );
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.plan.target).toEqual({ id: "aic-d12x-demo68", hostAbi: 1 });
    expect(readAicHostExtension(result.plan.hostExtension)).toEqual({ profileHash, tickHz: 60 });
  });

  test("rejects target ids that exceed the package table", () => {
    const invalid = { ...profile, id: "aic-target-name-too-long" };
    const result = validatePocketAicHostProfile(invalid);
    expect(result.ok).toBe(false);
    if (result.ok) return;
    expect(result.diagnostics).toContainEqual({
      code: "schema.maxLength",
      path: "/id",
      message: "maximum length is 15",
    });
  });

  test("rejects dynamic forms and unsupported rates", () => {
    expect(validatePocketAicHostProfile({ ...profile, form: "window" }).ok).toBe(false);
    expect(validatePocketAicHostProfile({ ...profile, tickHz: 0 }).ok).toBe(false);
  });

  test("rejects touch viewports that exceed the packed coordinate range", () => {
    const result = validatePocketAicHostProfile({
      ...profile,
      display: { ...profile.display, logicalViewports: [[513, 272]] },
    });
    expect(result.ok).toBe(false);
    if (result.ok) return;
    expect(result.diagnostics).toContainEqual({
      code: "aicHost.touchViewportTooLarge",
      path: "/display/logicalViewports/0",
      message: "touch-capable logical viewports must fit the 9-bit coordinate contract",
    });
  });
});
