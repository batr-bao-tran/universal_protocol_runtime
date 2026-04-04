load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_compiler",
    srcs = [
        "src/compiler/checksum_registry.cpp",
        "src/compiler/schema_compiler.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_authoring",
        "//packages/upr_core",
        "//packages/utils",
    ],
)
