// The retained GPUI host used by PocketCOM. Upstream's macos-app catalog
// entry describes the portable software host; it cannot advertise CoreText
// or the GPUI keyboard/IME/clipboard bridge. Keep this fork's host contract
// explicit while preserving the identity published by host/macos.
import {
  POCKET_CAPABILITIES,
  POCKET_TARGETS,
  definePlatformContractRegistry,
  defineTargetRegistry,
} from "../contracts/spec/platforms.ts";

export const MACOS_GPUI_CONTRACTS = definePlatformContractRegistry(
  POCKET_CAPABILITIES,
  defineTargetRegistry({
    "macos-app": {
      ...POCKET_TARGETS["macos-app"],
      capabilities: [
        "input.buttons",
        "input.text",
        "input.pointer",
        "input.ime",
        "host.clipboard",
        "display.viewport.live",
        "text.glyphs.baked",
        "text.glyphs.runtime",
        "text.layout.native",
      ],
    },
  }),
);
