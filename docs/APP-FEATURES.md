# Application feature specialization

Application feature profiles are separate from host capabilities. The optional compiler flags
`--app-features=<file> --app-profile=<name>` are accepted by `tools/pocket.ts` (check/compile/build)
and `tools/build.ts`. Both flags must be supplied together. Existing builds without app features
keep their existing pipeline.

A version 1 JSON registry supplies `module` (the application's feature import specifier),
`features` (definitions with optional `status`, `requires`, `modules`, `i18n`), `profiles` (an explicit boolean
for every implemented feature in every profile), and optional `resources` (JSON dictionaries). Paths are relative
to the registry. Unknown fields/features, missing values, nonboolean flags, broken dependencies/cycles,
overlapping ownership, and missing resource keys fail the build.

`status` is `implemented` (the compatibility default when omitted) or `planned`. It describes
integration with compile-time pruning, not whether the product functionality itself exists.
Planned entries reserve names in the one catalog; they cannot appear in profiles with either
boolean, own `modules`/`i18n`, or be referenced by `hasAppFeature()`. Implemented features cannot
depend on planned ones. Dependency-name and cycle validation still covers the entire catalog.
Promote an entry only after its actual pruning paths and tests have been integrated. Keep feature
names/states/values in this registry rather than duplicating them in code or documentation.

Applications declare the type of `hasAppFeature(name: keyof Profiles[keyof Profiles]): boolean` in an
ambient module matching `module`, deriving `Profiles` from the registry profiles, with a tsconfig paths mapping for entry-based type checks. There
is no guest runtime feature registry. Named imports, including aliases, are recognized by binding;
only direct calls with literal names are accepted. A local function with the same name is unaffected.
Do not pass the feature function as a value or use namespace imports/computed feature names.

The TS/TSX specializer replaces calls and folds syntactic boolean conditions before JSX lowering,
style/text collection and import traversal. It preserves lexical blocks. Removed branches with
hoisted var/function declarations are rejected. Vue SFCs are explicitly unsupported when this
specializer is enabled. Both passes use the same specialization and fingerprint in transform caching.

`modules` declares exclusive ownership, not a global sideEffects override. An import of a disabled
owned module can disappear only if no binding remains referenced and the specialized module has
no top-level execution effects. A conservative purity check rejects calls/new, evaluated member
accesses, getters, spreads, decorators, computed keys, static initialization and unsupported statements.
Move initialization behind feature guards or into factories. Pure function/class declarations are
allowed. Nonowned dependencies of removed modules remain side-effect imports, preserving their
initialization. A still-reachable disabled module (including via re-export) fails the build.

`i18n` names exact dotted keys or subtree prefixes owned by the feature. JSON `resources` are
filtered with that list, and the same filtered dictionaries contribute extra baked glyphs. Shared
keys must remain outside optional ownership. Class/text literals are collected from specialized
sources, not from removed modules. Explicit extra chars and other static resources are retained;
this is not a blanket unused-image/font-slot eliminator.

The host ResolvedBuildPlan schema/hash is unchanged. Product configuration identity is recorded
separately in `<output>.features.json` with the catalog/status and effective values, modules/excluded modules, omitted keys,
codepoint count, JS/PAK bytes and Bun metafile. Config/resource/excluded-module files enter compiler
dependency records. Choose separate output directories when producing different product profiles.
