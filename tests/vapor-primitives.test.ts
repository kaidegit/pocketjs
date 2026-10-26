import { expect, test } from "bun:test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { runInNewContext } from "node:vm";
import { jsxPlugin, transformFile } from "../framework/compiler/jsx-plugin.ts";
import { createWasmUi } from "../hosts/web/wasm-ops.js";

test("Vapor primitive lowering follows import bindings and preserves unsupported slot/ref paths", async () => {
  const source = `
import { View as Box, Text } from "@pocketjs/framework/components";
function Simple(p) { return <Box class="absolute" style={{width:p.width}} nodeRef={p.node}><Text>{p.text}</Text></Box>; }
function Shadow(Box) { return <Box />; }
function Spread(p) { return <Box {...p} />; }
function Ref(p) { return <Box class="absolute" ref={p.node} />; }
function Dynamic(p) { return <Box>{p.values.map(value=><Text>{value}</Text>)}</Box>; }
export {Simple,Shadow,Spread,Ref,Dynamic};`;
  const result = await transformFile("/tmp/vapor-primitives.tsx", source, "vue-vapor");
  expect(result.code).toContain('__createVaporPrimitive');
  expect(result.code).toContain('"view"');
  // Component-only refs/spreads and the dynamic `$` slot bag keep Vue proxies.
  expect(result.code.match(/_createComponent\(Box/g)?.length).toBe(3);
  expect(result.code).toContain('_createComponent(Text');
  const solid = await transformFile("/tmp/solid-primitives.tsx", source, "solid");
  expect(solid.code).not.toContain('__createVaporPrimitive');
});

test("batched native primitives preserve text, refs, press, theme geometry and disposed scope cleanup", async () => {
  const dir = await mkdtemp(join(tmpdir(), "pocketjs-vapor-primitives-"));
  try {
    const entry = join(dir, "main.tsx");
    await Bun.write(entry, `
import {ref} from "vue";
import {mount} from "@pocketjs/framework";
import {View as Box,Text} from "@pocketjs/framework/components";
import {rootMirror} from ${JSON.stringify(new URL("../framework/src/native-tree.ts", import.meta.url).pathname)};
let dispose;
function App(){
  const text=ref("before"),x=ref(4),disabled=ref(false),show=ref(true);
  const handle={current:null}; let presses=0;
  globalThis.change=patch=>{if("text" in patch)text.value=patch.text;if("x" in patch)x.value=patch.x;if("disabled" in patch)disabled.value=patch.disabled;if("show" in patch)show.value=patch.show;};
  globalThis.read=()=>({id:handle.current?.id,presses,nodes:walk(rootMirror)});
  return <Box style={{width:480,height:272}}>
    <Box nodeRef={handle} debugName="button" focusable={!disabled.value} onPress={()=>presses++}
      style={{posType:1,insetL:x.value,insetT:4,width:100,height:28,bgColor:"#555555"}}>
      <Text style={{fontSlot:0,lineHeight:28}}>{text.value}</Text>
    </Box>
    {show.value?<Box debugName="conditional" style={{posType:1,insetT:80,width:100,height:20}}><Text>{text.value}</Text></Box>:null}
  </Box>;
}
function walk(n){return [{id:n.id,text:n.text,debug:n.debugName,focusable:n.focusable,style:n.domAttrs?.style},...n.children.flatMap(walk)];}
globalThis.remount=()=>{dispose=mount(App,{styles:{}});};
globalThis.dispose=()=>dispose();
globalThis.remount();
`);
    const result = await Bun.build({ entrypoints: [entry], format: "iife", target: "browser", conditions: ["browser"],
      define: { "process.env.NODE_ENV": '"production"', document: "globalThis.__pocketDocument",
        __POCKET_TARGET__: '""', __POCKET_HOST_ABI__: "0", __POCKET_FEATURES__: "{}", __POCKET_PIXEL_RATIO__: "1" },
      plugins: [jsxPlugin("vue-vapor", { entry })] });
    expect(result.success).toBe(true);
    const wasm = await createWasmUi(await Bun.file(new URL("../hosts/web/pocketjs.wasm", import.meta.url)).arrayBuffer());
    let writes = 0;
    const setProp = wasm.ops.setProp;
    wasm.ops.setProp = (...args: Parameters<typeof setProp>) => { writes++; return setProp(...args); };
    type Snapshot = { id: number; presses: number; nodes: { id: number; text?: string; debug?: string; focusable?: boolean; style?: { insetL?: number } }[] };
    const guest = { ui: wasm.ops, console: { ...console, error: (e: unknown) => { throw e; } } } as unknown as {
      frame(buttons: number, analog?: number, touches?: number[], hits?: number[]): void;
      change(patch: Record<string, unknown>): void; read(): Snapshot; dispose(): void; remount(): void;
    };
    runInNewContext(await result.outputs[0]!.text(), guest);
    const frame = () => { guest.frame(0, 0); wasm.tick(); };
    const tap = (x: number) => {
      guest.frame(0, 0, [x | (12 << 9)], [wasm.ops.hitTestBounds(x, 12)]); wasm.tick(); frame(); frame();
    };
    frame();
    const id = guest.read().id;
    expect(id).toBeGreaterThan(1);
    tap(20); expect(guest.read().presses).toBe(1);
    guest.change({ text: "after", x: 120 }); frame();
    expect(guest.read().id).toBe(id);
    expect(guest.read().nodes.filter(n => n.text === "after").length).toBe(2);
    expect(guest.read().nodes.find(n => n.debug === "button")?.style?.insetL).toBe(120);
    guest.change({ disabled: true, show: false }); frame(); tap(140);
    expect(guest.read().presses).toBe(1);
    expect(guest.read().nodes.some(n => n.debug === "conditional")).toBe(false);
    guest.change({ disabled: false, show: true }); frame(); tap(140);
    expect(guest.read().presses).toBe(2);
    const oldChange = guest.change;
    guest.dispose(); guest.remount(); frame();
    const before = writes;
    oldChange({ text: "disposed", x: 250 }); frame();
    expect(writes).toBe(before);
    expect(guest.read().nodes.some(n => n.text === "disposed")).toBe(false);
    guest.dispose();
  } finally { await rm(dir, { recursive: true, force: true }); }
}, 30_000);
