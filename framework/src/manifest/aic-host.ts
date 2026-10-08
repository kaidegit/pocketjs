import { createHash } from "node:crypto";
import {
  POCKET_AIC_HOST_ABI,
  pocketAicHostSchema,
  type PocketAicHostProfile,
} from "../../../contracts/spec/aic-host.ts";
import {
  POCKET_CAPABILITIES,
  definePlatformContractRegistry,
  defineTargetRegistry,
  type PlatformContractRegistry,
} from "../../../contracts/spec/platforms.ts";
import { canonicalJson } from "./plan.ts";
import { createHostExtension, isHostExtension, type HostExtension } from "./host-extension.ts";
import { validateSchema, type ContractDiagnostic, type ValidationResult } from "./validate.ts";

export interface AicHostBuildPayload {
  readonly profileHash: string;
  readonly tickHz: number;
}

export function readAicHostExtension(extension: HostExtension | undefined): AicHostBuildPayload | undefined {
  if (extension === undefined) return undefined;
  if (!isHostExtension(extension)) throw new TypeError("invalid host extension identity");
  if (extension.kind !== "aic") return undefined;
  const { profileHash, tickHz } = extension.payload;
  if (extension.version !== 1 || typeof profileHash !== "string" ||
      !/^sha256:[0-9a-f]{64}$/.test(profileHash) || typeof tickHz !== "number" ||
      !Number.isInteger(tickHz) || tickHz < 1 || tickHz > 240) {
    throw new TypeError("invalid ArtInChip host extension payload/version");
  }
  return { profileHash, tickHz };
}

export function pocketAicHostExtension(profileHash: string, tickHz: number): HostExtension {
  const extension = createHostExtension("aic", 1, { profileHash, tickHz });
  readAicHostExtension(extension);
  return extension;
}

/** Platform environment projection belongs to this adapter, not generic hosts. */
export function aicHostBuildEnvironment(extension: HostExtension): Readonly<Record<string, string>> {
  const payload = readAicHostExtension(extension);
  if (!payload) throw new TypeError("expected ArtInChip host extension");
  return { POCKETJS_AIC_PROFILE_HASH: payload.profileHash, POCKETJS_TICK_HZ: String(payload.tickHz) };
}

export function validatePocketAicHostProfile(input: unknown): ValidationResult<PocketAicHostProfile> {
  const diagnostics: ContractDiagnostic[] = [];
  validateSchema(input, pocketAicHostSchema, "", diagnostics);
  if (diagnostics.length === 0) {
    const profile = input as PocketAicHostProfile;
    const encoded = new TextEncoder().encode(profile.id);
    if (encoded.length >= 16) {
      diagnostics.push({
        code: "aicHost.targetTooLong",
        path: "/id",
        message: "target id must occupy at most 15 UTF-8 bytes",
      });
    }
    if (profile.capabilities.includes("input.touch")) {
      profile.display.logicalViewports.forEach((viewport, index) => {
        if (viewport[0] > 512 || viewport[1] > 512) {
          diagnostics.push({
            code: "aicHost.touchViewportTooLarge",
            path: `/display/logicalViewports/${index}`,
            message: "touch-capable logical viewports must fit the 9-bit coordinate contract",
          });
        }
      });
    }
  }
  if (diagnostics.length > 0) return { ok: false, diagnostics };
  return { ok: true, value: input as PocketAicHostProfile };
}

export function hashPocketAicHostProfile(profile: PocketAicHostProfile): string {
  return `sha256:${createHash("sha256").update(canonicalJson(profile)).digest("hex")}`;
}

export function pocketAicHostRegistry(profile: PocketAicHostProfile): PlatformContractRegistry {
  return definePlatformContractRegistry(
    POCKET_CAPABILITIES,
    defineTargetRegistry({
      [profile.id]: {
        hostAbi: POCKET_AIC_HOST_ABI,
        platform: profile.platform,
        form: profile.form,
        display: profile.display,
        capabilities: profile.capabilities,
      },
    }),
  );
}
