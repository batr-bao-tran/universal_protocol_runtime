load("@aspect_rules_js//js:defs.bzl", "js_binary", "js_library", "js_run_binary", "js_test")
load("@aspect_rules_js//npm:defs.bzl", "npm_package")
load("@aspect_rules_ts//ts:defs.bzl", "ts_project")
load("@npm//:defs.bzl", "npm_link_all_packages")

package(default_visibility = ["//visibility:public"])

exports_files([
    "package.json",
    "README.md",
    "tsconfig.json",
])

npm_link_all_packages(name = "node_modules")

# Dependency-free TypeScript runtime. Compiled with the rules_ts managed
# TypeScript compiler; the emitted dist/ tree is byte-for-byte what ships to npm.
ts_project(
    name = "upr_typescript",
    # package.json is included so tsc's NodeNext module resolution sees
    # "type": "module" and emits ESM (matching the published package). It is not
    # matched by the tsconfig "include", so it is not compiled.
    srcs = glob(["src/**/*.ts"]) + ["package.json"],
    declaration = True,
    declaration_map = True,
    out_dir = "dist",
    root_dir = "src",
    source_map = True,
    transpiler = "tsc",
    tsconfig = "tsconfig.json",
    deps = [":node_modules/@types/node"],
)

# npm publishable package: dist/ + metadata, laid out at the package root so the
# "main"/"types" paths in package.json resolve.
npm_package(
    name = "npm_pkg",
    srcs = [
        "README.md",
        "package.json",
        ":upr_typescript",
    ],
    package = "universal-protocol-runtime",
)

# Shared, non-test helper imported by parity.test.ts.
js_library(
    name = "test_support",
    srcs = ["tests/generated_general_direct_codec.ts"],
)

# One js_test per test file. Each runs on the Node toolchain with runtime type
# stripping and imports the compiled runtime via ../dist/index.js.
js_test(
    name = "runtime_test",
    data = [":upr_typescript"],
    entry_point = "tests/runtime.test.ts",
    node_options = ["--experimental-strip-types"],
)

js_test(
    name = "parity_test",
    data = [
        ":test_support",
        ":upr_typescript",
    ],
    entry_point = "tests/parity.test.ts",
    node_options = ["--experimental-strip-types"],
)

# TypeScript line coverage. rules_js' built-in `bazel coverage` merger only
# attributes coverage to the test files themselves (it drops the compiled
# dist/*.js), so this target runs the suite under V8 coverage and uses monocart
# to remap dist/*.js back onto the original src/*.ts. js_run_binary is a stock
# rules_js rule; the runner is a plain script (no custom Starlark rule).
js_binary(
    name = "coverage_bin",
    data = [
        "tests/parity.test.ts",
        "tests/runtime.test.ts",
        ":node_modules/monocart-coverage-reports",
        ":test_support",
        ":upr_typescript",
    ],
    entry_point = "tests/coverage_runner.mjs",
)

js_run_binary(
    name = "coverage",
    srcs = [],
    outs = ["coverage/lcov.info"],
    args = ["coverage/lcov.info"],
    chdir = package_name(),
    tool = ":coverage_bin",
)
