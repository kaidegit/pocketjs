import type { PluginObj } from "@babel/core";
import * as t from "@babel/types";

/** Imported host primitives have no component state or lifecycle. Keep their
 * native nodes and prop effect in the caller's Vapor scope, without allocating
 * a component instance/attrs proxy per decoration. Other components, spreads,
 * refs and shadowed identifiers retain the ordinary component path. */
export function lowerVaporPrimitives(): PluginObj {
  return {
    name: "pocketjs-vapor-primitives",
    visitor: {
      Program: {
        exit(program) {
          const primitives = new Map<string, string>();
          const factories = new Set<string>();
          let source: string | undefined;
          for (const statement of program.node.body) {
            if (!t.isImportDeclaration(statement)) continue;
            const path = statement.source.value;
            for (const specifier of statement.specifiers) {
              if (!t.isImportSpecifier(specifier) || !t.isIdentifier(specifier.imported)) continue;
              if (path === "/vue-jsx-vapor/vapor" && specifier.imported.name === "createComponent") factories.add(specifier.local.name);
              if (["@pocketjs/framework/components", "@pocketjs/framework/vue-vapor/components"].includes(path)
                  && ["View", "Text"].includes(specifier.imported.name)) {
                primitives.set(specifier.local.name, specifier.imported.name.toLowerCase());
                source = path;
              }
            }
          }
          if (!source || !primitives.size || !factories.size) return;
          const helper = program.scope.generateUidIdentifier("pocketPrimitive");
          let lowered = false;
          program.traverse({
            CallExpression(path) {
              const { callee, arguments: args } = path.node;
              if (!t.isIdentifier(callee) || !factories.has(callee.name)
                  || path.scope.getBinding(callee.name) !== program.scope.getBinding(callee.name)) return;
              const tag = args[0];
              if (!t.isIdentifier(tag) || !primitives.has(tag.name)
                  || path.scope.getBinding(tag.name) !== program.scope.getBinding(tag.name)) return;
              // Template refs are emitted as separate setter calls against the
              // result variable. Keep component identity for such consumers.
              const variable = path.parentPath;
              if (variable.isVariableDeclarator() && t.isIdentifier(variable.node.id)) {
                const binding = path.scope.getBinding(variable.node.id.name);
                if (binding?.referencePaths.some(ref => ref.parentPath?.isCallExpression())) return;
              }
              const props = args[1];
              if (props && !t.isNullLiteral(props) && (!t.isObjectExpression(props) || props.properties.some(prop =>
                !t.isObjectProperty(prop) || prop.computed ||
                ["ref", "$"].includes(t.isIdentifier(prop.key) ? prop.key.name : t.isStringLiteral(prop.key) ? prop.key.value : "ref")))) return;
              const slots = args[2];
              // `$`/named slot bags need Vue's slot proxy and normalization.
              // A plain function (optionally Vue.extend-marked) already has
              // the same shape accepted by the native primitive boundary.
              if (slots && !(t.isFunctionExpression(slots) || t.isArrowFunctionExpression(slots)
                  || t.isNullLiteral(slots) || (t.isIdentifier(slots, { name: "undefined" }) && !path.scope.getBinding("undefined"))
                  || (t.isCallExpression(slots) && t.isIdentifier(slots.callee)
                    && (() => {
                      const binding = path.scope.getBinding(slots.callee.name);
                      return binding?.path.isImportSpecifier() && t.isIdentifier(binding.path.node.imported, { name: "extend" })
                        && binding.path.parentPath.isImportDeclaration() && binding.path.parentPath.node.source.value === "vue";
                    })()))) return;
              path.replaceWith(t.callExpression(helper, [t.stringLiteral(primitives.get(tag.name)!),
                !props || t.isNullLiteral(props) ? t.objectExpression([]) : props, slots ?? t.identifier("undefined")]));
              lowered = true;
            },
          });
          if (lowered) program.unshiftContainer("body", t.importDeclaration([
            t.importSpecifier(helper, t.identifier("__createVaporPrimitive")),
          ], t.stringLiteral(source)));
        },
      },
    },
  };
}
