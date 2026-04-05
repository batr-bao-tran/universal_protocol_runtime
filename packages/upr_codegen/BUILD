load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_codegen",
    srcs = [
        "src/codegen/bindings_generator.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_compiler",
        "//packages/utils",
    ],
)

cc_binary(
    name = "generate_bindings",
    srcs = ["tools/generate_bindings_main.cpp"],
    deps = [
        ":upr_codegen",
        "//packages/upr_authoring",
        "//packages/upr_compiler",
    ],
)
