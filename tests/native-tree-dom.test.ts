import { beforeEach, describe, expect, test } from "bun:test";
import { installHost, type HostOps } from "../framework/src/host.ts";
import {
  adoptNativeRoot,
  createCommentNode,
  createElement,
  createTextNode,
  decorateNativeNode,
  isNativeNode,
  release,
  releaseNativeRoot,
  resetRendererState,
  retain,
  rootMirror,
  runSweep,
  setStyleResolver,
  type NodeMirror,
} from "../framework/src/native-tree.ts";

type DomNode = NodeMirror & {
  nodeType: number;
  nodeName: string;
  tagName: string;
  className: string;
  nodeValue: string;
  data: string;
  textContent: string;
  isConnected: boolean;
  parentNode: NodeMirror | null;
  parentElement: NodeMirror | null;
  childNodes: NodeMirror[];
  firstChild: NodeMirror | null;
  lastChild: NodeMirror | null;
  nextSibling: NodeMirror | null;
  previousSibling: NodeMirror | null;
  style: { length: number; item(): string };
  classList: { add(): void; remove(): void };
  appendChild(child: NodeMirror): NodeMirror;
  insertBefore(child: NodeMirror, anchor?: NodeMirror | null): NodeMirror;
  removeChild(child: NodeMirror): NodeMirror;
  replaceChild(next: NodeMirror, current: NodeMirror): NodeMirror;
  cloneNode(deep?: boolean): DomNode;
  remove(): void;
  contains(other: NodeMirror | null): boolean;
  hasChildNodes(): boolean;
  setAttribute(name: string, value: unknown): void;
  getAttribute(name: string): string | null;
  hasAttribute(name: string): boolean;
  removeAttribute(name: string): void;
};

const dom = (node: NodeMirror): DomNode => node as DomNode;
let destroyed: number[];
let replaced: [number, string][];

beforeEach(() => {
  resetRendererState();
  destroyed = [];
  replaced = [];
  let nextId = 10;
  installHost({
    kind: "injected", target: "test", strict: true,
    ops: {
      createNode: () => nextId++,
      setText() {},
      setStyle() {},
      replaceText: (id: number, text: string) => { replaced.push([id, text]); },
      insertBefore() {},
      removeChild() {},
      destroyNode: (id: number) => { destroyed.push(id); },
    } as unknown as HostOps,
  });
});

describe("native node DOM facade", () => {
  test("shares functions for every creation path while isolating mutable containers", () => {
    const element = dom(createElement("view"));
    const nodes = [element, dom(createTextNode("a")), dom(createCommentNode("b")),
      element.cloneNode(), dom(adoptNativeRoot(2)), dom(rootMirror)];
    for (const node of nodes) {
      expect(isNativeNode(node)).toBe(true);
      expect(node.appendChild).toBe(element.appendChild);
      expect(node.cloneNode).toBe(element.cloneNode);
      expect(node.style.item).toBe(element.style.item);
      expect(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(node), "nodeValue")?.get)
        .toBe(Object.getOwnPropertyDescriptor(Object.getPrototypeOf(element), "nodeValue")?.get);
    }
    const other = nodes[1];
    expect(other.children).not.toBe(element.children);
    expect(other.style).not.toBe(element.style);
    expect(other.classList).not.toBe(element.classList);
    element.style.length = 1;
    expect(other.style.length).toBe(0);
    element.setAttribute("title", "one");
    expect(other.getAttribute("title")).toBeNull();
    const style = element.style;
    expect(decorateNativeNode(element)).toBe(element);
    expect(element.style).toBe(style);
  });

  test("uses the receiver when a DOM method is borrowed", () => {
    const first = dom(createElement("view"));
    const second = dom(createElement("view"));
    const child = createTextNode("child");
    expect(first.appendChild.call(second, child)).toBe(child);
    expect(first.children).toEqual([]);
    expect(second.children).toEqual([child]);
    first.setAttribute.call(second, "title", "second");
    expect(first.getAttribute("title")).toBeNull();
    expect(second.getAttribute("title")).toBe("second");
  });

  test("keeps parent, sibling and connected getters current through moves and replacements", () => {
    const parent = dom(createElement("view"));
    const other = dom(adoptNativeRoot(2));
    const a = dom(createTextNode("a"));
    const b = dom(createCommentNode("anchor"));
    dom(rootMirror).appendChild(parent);
    parent.appendChild(a);
    parent.appendChild(b);
    expect(parent.firstChild).toBe(a);
    expect(parent.lastChild).toBe(b);
    expect(a.nextSibling).toBe(b);
    expect(b.previousSibling).toBe(a);
    expect(a.parentNode).toBe(parent);
    expect(a.parentElement).toBe(parent);
    expect(parent.childNodes).toBe(parent.children);
    expect(a.isConnected).toBe(true);
    expect(parent.contains(a)).toBe(true);
    other.insertBefore(a, null);
    expect(b.previousSibling).toBeNull();
    expect(parent.contains(a)).toBe(false);
    expect(a.parentNode).toBe(other);
    expect(a.isConnected).toBe(true);
    expect(other.replaceChild(b, a)).toBe(a);
    expect(a.isConnected).toBe(false);
    expect(parent.hasChildNodes()).toBe(false);
    b.remove();
    expect(b.isConnected).toBe(false);
    releaseNativeRoot(other);
    expect(other.isConnected).toBe(false);
  });

  test("clones element attributes, text and comment anchors without sharing mutable state", () => {
    const parent = dom(createElement("view"));
    parent.setAttribute("title", "original");
    parent.appendChild(createTextNode("hello"));
    parent.appendChild(createCommentNode("anchor"));
    const clone = parent.cloneNode(true);
    expect(clone.id).not.toBe(parent.id);
    expect(clone.getAttribute("title")).toBe("original");
    expect(clone.children).toHaveLength(2);
    expect(dom(clone.children[0]).nodeName).toBe("#text");
    expect(dom(clone.children[0]).data).toBe("hello");
    expect(dom(clone.children[1]).nodeName).toBe("#comment");
    expect(dom(clone.children[1]).data).toBe("anchor");
    clone.setAttribute("title", "copy");
    clone.removeAttribute("title");
    expect(clone.hasAttribute("title")).toBe(false);
    expect(parent.getAttribute("title")).toBe("original");
    dom(clone.children[0]).data = "changed";
    expect(dom(parent.children[0]).data).toBe("hello");
    expect(parent.cloneNode().children).toEqual([]);
  });

  test("routes inherited class accessors through the compiled style table", () => {
    setStyleResolver((name) => name === "known" ? 7 : undefined);
    try {
      const node = dom(createElement("view"));
      const other = dom(createElement("view"));
      node.className = "known";
      expect(node.tagName).toBe("VIEW");
      expect(node.className).toBe("known");
      expect(other.className).toBe("");
      expect(node.cloneNode().className).toBe("known");
      node.removeAttribute("class");
      expect(node.className).toBe("");
    } finally {
      setStyleResolver(() => undefined);
    }
  });

  test("routes text setters to their node and keeps comment payload out of painted text", () => {
    const text = dom(createTextNode("old"));
    const comment = dom(createCommentNode("anchor"));
    text.nodeValue = "one";
    text.data = "two";
    text.textContent = "three";
    expect(text.data).toBe("three");
    expect(replaced).toEqual([[text.id, "one"], [text.id, "two"], [text.id, "three"]]);
    comment.nodeValue = "first";
    comment.data = "second";
    comment.textContent = "third";
    expect(comment.data).toBe("third");
    expect(comment.text).toBe("");
    expect(comment.nodeType).toBe(8);
    expect(replaced).toHaveLength(3);
    const container = dom(createElement("view"));
    container.appendChild(text);
    container.textContent = "replacement";
    expect(text.parentNode).toBeNull();
    expect(container.children).toHaveLength(1);
    expect(dom(container.firstChild!).data).toBe("replacement");
  });

  test("preserves retain, reattachment and detached subtree sweeping", () => {
    const parent = dom(createElement("view"));
    const child = dom(createTextNode("retained"));
    dom(rootMirror).appendChild(parent);
    parent.appendChild(child);
    retain(child);
    dom(rootMirror).removeChild(parent);
    runSweep();
    expect(destroyed).toEqual([]);
    release(child);
    dom(rootMirror).appendChild(parent);
    runSweep();
    expect(destroyed).toEqual([]);
    parent.remove();
    runSweep();
    expect(destroyed).toEqual([parent.id]);
    expect(parent.isConnected).toBe(false);
  });
});
