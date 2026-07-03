load("@aspect_rules_js//npm:defs.bzl", "npm_link_package")

# Root package. Exposes files that repository rules need to reference by label
# (e.g. rules_js verifies node_modules directories are listed in .bazelignore).
# MODULE.bazel is exported so schema codegen can stage it as a workspace-root
# marker (upr-gen resolves workspace-relative schema imports from it).
exports_files([
    ".bazelignore",
    "MODULE.bazel",
])

# First-party runtime linked at the workspace root so any consumer can resolve
# `universal-protocol-runtime` import via Node's upward node_modules lookup.
# Linking a first-party npm_package with `src` is only permitted in the root package.
npm_link_package(
    name = "node_modules/universal-protocol-runtime",
    src = "//packages/upr_typescript:npm_pkg",
    visibility = ["//visibility:public"],
)
