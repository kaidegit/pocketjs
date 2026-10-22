// Generic application features, independent of host capabilities.
import { transformAsync, type PluginObj } from "@babel/core";
import { realpathSync } from "node:fs";
import { dirname, resolve } from "node:path";

interface Feature { status?: "implemented" | "planned"; requires?: string[]; modules?: string[]; i18n?: string[] }
export interface AppFeatures {
  file: string;
  root: string;
  module: string;
  profile: string;
  values: Record<string, boolean>;
  definitions: Record<string, Feature>;
  resources: string[];
  disabledModules: Set<string>;
  omittedKeys: string[];
  fingerprint: string;
  excluded: Set<string>;
}
function record(value: unknown): value is Record<string, any> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}
export async function loadAppFeatures(file: string, profile: string): Promise<AppFeatures> {
  const raw = await Bun.file(file).json();
  if (!record(raw) || raw.version !== 1 || typeof raw.module !== "string" ||
    !record(raw.features) || !record(raw.profiles) || !record(raw.profiles[profile])) {
    throw new Error(`invalid app features config or unknown profile: ${profile}`);
  }
  if (Object.keys(raw).some(k => !["version", "module", "features", "profiles", "resources"].includes(k))) {
    throw new Error("unknown app features config field");
  }
  const names = Object.keys(raw.features).sort();
  for (const [name, definition] of Object.entries(raw.features)) {
    if (!record(definition) || Object.keys(definition).some(k => !["status", "requires", "modules", "i18n"].includes(k))) {
      throw new Error(`invalid feature definition: ${name}`);
    }
    if (definition.status !== undefined && !["implemented", "planned"].includes(definition.status))
      throw new Error(`invalid feature status: ${name}`);
    if (definition.status === "planned" && (definition.modules !== undefined || definition.i18n !== undefined))
      throw new Error(`planned feature cannot own modules or resources: ${name}`);
    for (const field of ["requires", "modules", "i18n"]) {
      if (definition[field] !== undefined && (!Array.isArray(definition[field]) ||
        !definition[field].every((x: unknown) => typeof x === "string"))) throw new Error(`invalid ${name}.${field}`);
    }
    for (const dep of definition.requires ?? []) if (!names.includes(dep)) throw new Error(`unknown dependency: ${dep}`);
  }
  const implemented = names.filter(name => raw.features[name].status !== "planned");
  for (const name of implemented) for (const dep of raw.features[name].requires ?? []) {
    if (raw.features[dep].status === "planned") throw new Error(`${name} requires planned feature ${dep}`);
  }
  // Validate every profile, so a newly added feature cannot leave old targets ambiguous.
  for (const [name, flags] of Object.entries(raw.profiles)) {
    if (record(flags)) for (const feature of Object.keys(flags)) {
      if (raw.features[feature]?.status === "planned") throw new Error(`planned feature cannot be configured in profile ${name}: ${feature}`);
    }
    if (!record(flags) || JSON.stringify(Object.keys(flags).sort()) !== JSON.stringify(implemented) ||
      !Object.values(flags).every(v => typeof v === "boolean")) throw new Error(`incomplete/unknown feature in profile ${name}`);
    for (const feature of implemented) if (flags[feature]) {
      for (const dep of raw.features[feature].requires ?? []) if (!flags[dep]) throw new Error(`${name}: ${feature} requires ${dep}`);
    }
  }
  // Cycles indicate a broken feature registry even when both features are disabled.
  const done = new Set<string>(), active = new Set<string>();
  function visit(name: string) {
    if (active.has(name)) throw new Error(`cyclic feature dependency: ${name}`);
    if (done.has(name)) return;
    active.add(name);
    for (const dep of raw.features[name].requires ?? []) visit(dep);
    active.delete(name); done.add(name);
  }
  for (const name of names) visit(name);
  if (raw.resources !== undefined && (!Array.isArray(raw.resources) ||
    !raw.resources.every((v: unknown) => typeof v === "string"))) throw new Error("invalid feature resources");
  const root = dirname(realpathSync(file));
  const owners = new Set<string>(), keys = new Set<string>(), disabledModules = new Set<string>(), omittedKeys: string[] = [];
  for (const name of implemented) {
    for (const path of raw.features[name].modules ?? []) {
      const absolute = resolve(root, path);
      if (owners.has(absolute)) throw new Error(`feature module has multiple owners: ${path}`);
      owners.add(absolute);
      if (!raw.profiles[profile][name]) disabledModules.add(absolute);
    }
    for (const key of raw.features[name].i18n ?? []) {
      if ([...keys].some(k => k === key || k.startsWith(key + ".") || key.startsWith(k + "."))) throw new Error(`overlapping feature resource key: ${key}`);
      keys.add(key);
      if (!raw.profiles[profile][name]) omittedKeys.push(key);
    }
  }
  const resources: string[] = (raw.resources ?? []).map((p: string) => resolve(root, p));
  for (const file of resources) {
    const dictionary = await Bun.file(file).json();
    for (const key of keys) {
      let node = dictionary;
      if (!record(node) || !Object.hasOwn(node, key)) {
        for (const part of key.split(".")) node = record(node) ? node[part] : undefined;
      } else node = node[key];
      if (node === undefined) throw new Error(`feature resource key ${key} missing in ${file}`);
    }
  }
  const canonical = (v: any): any => Array.isArray(v) ? v.map(canonical) : record(v)
    ? Object.fromEntries(Object.keys(v).sort().map(k => [k, canonical(v[k])])) : v;
  const fingerprint = new Bun.CryptoHasher("sha256").update(JSON.stringify(canonical({ raw, profile, specializer: 1 }))).digest("hex");
  return { file: resolve(file), root, module: raw.module, profile, values: raw.profiles[profile], definitions: raw.features,
    resources, disabledModules, omittedKeys, fingerprint, excluded: new Set() };
}

export function filterFeatureResource(value: unknown, features: AppFeatures): unknown {
  function walk(node: any, prefix: string): any {
    if (!record(node)) return node;
    return Object.fromEntries(Object.entries(node).flatMap(([key, child]) => {
      const path = prefix ? `${prefix}.${key}` : key;
      return features.omittedKeys.some(k => path === k || path.startsWith(k + ".")) ? [] : [[key, walk(child, path)]];
    }));
  }
  return walk(value, "");
}

function folder(features: AppFeatures): PluginObj {
  return { name: "app-feature-specialization", visitor: { Program(program) {
    program.traverse({ CallExpression(path) {
      const callee = path.get("callee");
      if (!callee.isIdentifier()) return;
      const binding = path.scope.getBinding(callee.node.name);
      if (!binding?.path.isImportSpecifier() || !binding.path.parentPath?.isImportDeclaration() ||
        binding.path.parentPath.node.source.value !== features.module) return;
      const imported = binding.path.node.imported;
      if ((imported.type === "Identifier" ? imported.name : imported.value) !== "hasAppFeature") return;
      const arg = path.node.arguments[0];
      if (path.node.arguments.length !== 1 || arg?.type !== "StringLiteral") throw path.buildCodeFrameError("hasAppFeature requires one literal feature name");
      if (features.definitions[arg.value]?.status === "planned") throw path.buildCodeFrameError(`app feature is only planned, not integrated: ${arg.value}`);
      if (!Object.hasOwn(features.values, arg.value)) throw path.buildCodeFrameError(`unknown app feature: ${arg.value}`);
      path.replaceWith({ type: "BooleanLiteral", value: features.values[arg.value]! });
    }});
    // Exit-order folding keeps arbitrary effectful expressions intact. Only syntactic
    // booleans are folded, rather than Babel evaluate() on application expressions.
    program.traverse({
      LogicalExpression: { exit(path) {
        const left = path.node.left;
        if (left.type !== "BooleanLiteral") return;
        if (path.node.operator === "&&") path.replaceWith(left.value ? path.node.right : left);
        else if (path.node.operator === "||") path.replaceWith(left.value ? left : path.node.right);
      }},
      UnaryExpression: { exit(path) {
        if (path.node.operator === "!" && path.node.argument.type === "BooleanLiteral")
          path.replaceWith({ type: "BooleanLiteral", value: !path.node.argument.value });
      }},
      ConditionalExpression: { exit(path) {
        if (path.node.test.type === "BooleanLiteral") path.replaceWith(path.node.test.value ? path.node.consequent : path.node.alternate);
      }},
      IfStatement: { exit(path) {
        if (path.node.test.type !== "BooleanLiteral") return;
        // Preserve var/function hoisting semantics rather than silently change them.
        const removed = path.node.test.value ? path.get("alternate") : path.get("consequent");
        if (removed.isVariableDeclaration({ kind: "var" }) || removed.isFunctionDeclaration())
          throw removed.buildCodeFrameError("feature branches must not remove hoisted declarations");
        if (removed.node) removed.traverse({ VariableDeclaration(p) {
          if (p.node.kind === "var") throw p.buildCodeFrameError("feature branches must use let/const, not hoisted var");
        }, FunctionDeclaration(p) { throw p.buildCodeFrameError("feature branches must not contain hoisted function declarations"); }});
        const keep = path.node.test.value ? path.node.consequent : path.node.alternate;
        if (keep) path.replaceWith(keep); else path.remove();
      }},
    });
    program.scope.crawl();
    for (const path of program.get("body")) if (path.isImportDeclaration() && path.node.source.value === features.module) {
      for (const spec of path.node.specifiers) {
        if (spec.type !== "ImportSpecifier" || (spec.imported.type === "Identifier" ? spec.imported.name : spec.imported.value) !== "hasAppFeature")
          throw path.buildCodeFrameError("feature module only exports hasAppFeature");
        if (program.scope.getBinding(spec.local.name)?.referenced) throw path.buildCodeFrameError("hasAppFeature must be called directly with a literal");
      }
      path.remove();
    }
  }}};
}

function resolveSource(from: string, spec: string): string | undefined {
  if (!spec.startsWith(".") && !spec.startsWith("/")) return undefined;
  try { return Bun.resolveSync(spec, dirname(from)); } catch { return undefined; }
}
// Strict allow-list: no calls/new, getters, spreads, computed keys, assignments
// or evaluated class static members. Instance fields execute only on construction.
function pure(node: any): boolean {
  if (!node) return true;
  switch (node.type) {
    case "Identifier": case "StringLiteral": case "NumericLiteral": case "BooleanLiteral": case "NullLiteral":
    case "FunctionExpression": case "ArrowFunctionExpression": case "FunctionDeclaration": return true;
    case "TSAsExpression": case "TSNonNullExpression": case "TSSatisfiesExpression": return pure(node.expression);
    case "ArrayExpression": return node.elements.every(pure);
    case "ObjectExpression": return node.properties.every((p: any) => p.type === "ObjectProperty" && !p.computed && pure(p.value));
    case "UnaryExpression": return ["!", "+", "-", "~", "void", "typeof"].includes(node.operator) && pure(node.argument);
    case "BinaryExpression": case "LogicalExpression": return pure(node.left) && pure(node.right);
    case "ConditionalExpression": return pure(node.test) && pure(node.consequent) && pure(node.alternate);
    case "VariableDeclaration": return node.declarations.every((d: any) => pure(d.init));
    case "ClassDeclaration": return !node.superClass && !node.decorators?.length && node.body.body.every((p: any) =>
      p.type !== "StaticBlock" && !p.computed && !p.decorators?.length && (!p.static || p.type === "ClassMethod"));
    case "ExportNamedDeclaration": return !node.declaration || pure(node.declaration);
    case "ImportDeclaration": case "ExportAllDeclaration": case "EmptyStatement": return true;
    case "TSInterfaceDeclaration": case "TSTypeAliasDeclaration": case "TSDeclareFunction": return true;
    default: return false;
  }
}

async function fold(path: string, src: string, features: AppFeatures) {
  if (path.endsWith(".vue")) throw new Error("app feature specialization currently supports TS/TSX, not Vue SFC");
  const result = await transformAsync(src, { filename: path, ast: true,
    parserOpts: { plugins: ["typescript", "jsx"] }, plugins: [folder(features)], babelrc: false, configFile: false });
  if (!result?.ast || result.code === undefined || result.code === null) throw new Error(`feature transform failed: ${path}`);
  return result;
}

export async function specializeAppSource(path: string, src: string, features: AppFeatures): Promise<string> {
  const result = await fold(path, src, features);
  const effects = new Map<string, string[]>();
  async function omittedModule(file: string, active = new Set<string>()): Promise<string[]> {
    if (active.has(file)) throw new Error(`cyclic disabled feature modules: ${file}`);
    if (effects.has(file)) return effects.get(file)!;
    active.add(file);
    const specialized = await fold(file, await Bun.file(file).text(), features);
    const body = specialized.ast!.program.body;
    for (const statement of body) if (!pure(statement)) throw new Error(`disabled feature module has top-level effects: ${file}`);
    const deps = new Set<string>();
    for (const statement of body) {
      if (statement.type !== "ImportDeclaration" && statement.type !== "ExportNamedDeclaration" && statement.type !== "ExportAllDeclaration") continue;
      if (!statement.source || (statement.type === "ImportDeclaration" && statement.importKind === "type") ||
        (statement.type !== "ImportDeclaration" && statement.exportKind === "type")) continue;
      const spec = statement.source.value;
      const dep = resolveSource(file, spec);
      if (dep && features.disabledModules.has(dep)) for (const effect of await omittedModule(dep, active)) deps.add(effect);
      else deps.add(dep ?? spec);
    }
    features.excluded.add(file);
    active.delete(file);
    effects.set(file, [...deps]);
    return [...deps];
  }
  // A second Babel pass sees fresh bindings after feature branches disappeared.
  const removals = new Map<string, string[]>();
  for (const statement of result.ast!.program.body) {
    if (statement.type !== "ImportDeclaration") continue;
    const file = resolveSource(path, statement.source.value);
    if (file && features.disabledModules.has(file)) removals.set(statement.source.value, await omittedModule(file));
  }
  if (removals.size === 0) return result.code!;
  const prune: PluginObj = { visitor: { Program(program) {
    program.scope.crawl();
    for (const imp of program.get("body")) if (imp.isImportDeclaration() && removals.has(imp.node.source.value)) {
      for (const spec of imp.node.specifiers) if (program.scope.getBinding(spec.local.name)?.referenced)
        throw imp.buildCodeFrameError(`disabled feature still referenced: ${imp.node.source.value}`);
      const deps = removals.get(imp.node.source.value)!;
      imp.replaceWithMultiple(deps.map(value => ({ type: "ImportDeclaration" as const, specifiers: [], source: { type: "StringLiteral" as const, value } })));
    }
  }}};
  const pruned = await transformAsync(result.code!, { filename: path, parserOpts: { plugins: ["typescript", "jsx"] }, plugins: [prune], babelrc: false, configFile: false });
  return pruned!.code!;
}
