// Real Vue Vapor + native core bounds hits: a cached off-screen Portal must
// let touches reach the app again, while its visible scrim still blocks them.
// Run after bun tools/wasm.ts: bun test tests/portal-hit-vue-vapor.test.ts
import { expect, test } from "bun:test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { runInNewContext } from "node:vm";
import { jsxPlugin } from "../framework/compiler/jsx-plugin.ts";
import { createWasmUi } from "../platforms/web/wasm-ops.js";

test("a cached Vue Vapor Portal can reopen through touch after either close action", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pocketjs-vapor-portal-"));
  try {
    const entry = join(dir, "main.tsx");
    await Bun.write(entry, `
import { ref } from "vue";
import { mount } from "@pocketjs/framework";
import { Portal, View } from "@pocketjs/framework/components";
const open = ref(false), mounted = ref(false);
let mounts = 0, opens = 0;
function Drawer() {
  mounts++;
  return <Portal>
    <View style={{posType: 1, insetL: open.value ? 0 : 480, insetT: 0, width: 480, height: 272}}>
      <View focusable onPress={() => { open.value = false; }}
        style={{posType: 1, insetL: 0, insetT: 0, width: 480, height: 272, bgColor: "#00000080"}} />
      <View style={{posType: 1, insetL: 0, insetT: 0, width: 272, height: 272, bgColor: "#222222"}}>
        <View focusable onPress={() => { open.value = false; }}
          style={{posType: 1, insetL: 216, insetT: 8, width: 44, height: 28, bgColor: "#555555"}} />
      </View>
    </View>
  </Portal>;
}
function App() {
  return <View style={{width: 480, height: 272}}>
    <View focusable onPress={() => { mounted.value = true; open.value = true; opens++; }}
      style={{posType: 1, insetL: 8, insetT: 4, width: 104, height: 24, bgColor: "#555555"}} />
    {mounted.value ? <Drawer /> : null}
  </View>;
}
globalThis.readState = () => ({open: open.value, mounts, opens});
globalThis.dispose = mount(App, {styles: {}});
`);
    const result = await Bun.build({
      entrypoints: [entry], format: "iife", target: "browser", conditions: ["browser"],
      define: {
        "process.env.NODE_ENV": '"production"', document: "globalThis.__pocketDocument",
        __POCKET_TARGET__: '""', __POCKET_HOST_ABI__: "0",
        __POCKET_FEATURES__: "{}", __POCKET_PIXEL_RATIO__: "1",
      },
      plugins: [jsxPlugin("vue-vapor", { entry })],
    });
    expect(result.success).toBe(true);
    const wasm = await createWasmUi(await Bun.file(new URL("../platforms/web/pocketjs.wasm", import.meta.url)).arrayBuffer());
    const guest = { ui: wasm.ops, console } as typeof globalThis & {
      frame: (buttons: number, analog?: number, touches?: number[], hits?: number[]) => void;
      readState: () => { open: boolean; mounts: number; opens: number };
      dispose: () => void;
    };
    runInNewContext(await result.outputs[0]!.text(), guest);
    const frame = (touches?: number[], hits?: number[]) => {
      guest.frame(0, 0, touches, hits);
      wasm.tick();
    };
    const tap = (x: number, y: number) => {
      // Same down-edge bounds fact and release semantics as the AIC host.
      frame([x | (y << 9)], [wasm.ops.hitTestBounds(x, y)]);
      frame();
      frame();
    };
    try {
      frame();
      const opener = wasm.ops.hitTestBounds(60, 16);
      expect(opener).toBeGreaterThan(0);
      for (let cycle = 1; cycle <= 4; cycle++) {
        tap(60, 16);
        expect(guest.readState()).toEqual({ open: true, mounts: 1, opens: cycle });
        // Visible panel blocks the opener underneath it.
        tap(60, 16);
        expect(guest.readState()).toEqual({ open: true, mounts: 1, opens: cycle });
        if (cycle % 2) tap(238, 22); // Back button.
        else tap(450, 140); // Scrim outside the panel.
        expect(guest.readState()).toEqual({ open: false, mounts: 1, opens: cycle });
        expect(wasm.ops.hitTestBounds(60, 16)).toBe(opener);
      }
    } finally {
      guest.dispose();
    }
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}, 30_000);
