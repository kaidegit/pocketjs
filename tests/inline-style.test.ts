import { expect, test } from "bun:test";
import { installHost, type HostOps } from "../framework/src/host";
import { createElement, setProp, resetRendererState } from "../framework/src/native-tree";
import { PROP } from "../contracts/spec/spec";

test("inline styles remain direct with optional batch API, preserving encoding/order, deltas and errors", () => {
  function run(batch: boolean) {
    resetRendererState();
    const writes: number[][] = []; let calls = 0, id = 10;
    const ops = { createNode: () => id++, setProp: (...args: number[]) => { calls++; writes.push(args); } } as unknown as HostOps;
    if (batch) ops.setPropBatch = _buffer => {
      throw new Error("inline styles must not auto-pack batches");
    };
    installHost({ kind: "injected", target: "test", strict: true, ops });
    const node = createElement("view");
    const style = { width: 30.5, height: 20, textColor: "#123456", opacity: .5 };
    setProp(node, "style", style);
    const first = calls;
    setProp(node, "style", { ...style }, style);
    expect(calls).toBe(first);
    setProp(node, "style", { ...style, width: 50 }, style);
    expect(calls).toBe(first + 1);
    // A later bad key retains earlier valid writes, just like scalar setProp.
    expect(() => setProp(node, "style", { height: 21, width: 51, invalid: 3 })).toThrow("unknown style prop");
    expect(() => setProp(node, "style", { width: 52, height: "bad" })).toThrow("non-numeric");
    expect(writes.at(-1)).toEqual([node.id, PROP.width, 52]);
    return { writes, first, calls };
  }
  const scalar = run(false), batch = run(true);
  expect(batch.writes).toEqual(scalar.writes);
  expect(scalar.first).toBe(4);
  expect(batch.first).toBe(4);
  expect(batch.calls).toBe(scalar.calls);
});
