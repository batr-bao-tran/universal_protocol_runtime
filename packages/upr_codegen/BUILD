load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_codegen",
    srcs = [
        "src/codegen/bindings_generator.cpp",
        "src/codegen/bindings_generator_common.inc",
        "src/codegen/bindings_generator_cpp.inc",
        "src/codegen/bindings_generator_cpp_module.inc",
        "src/codegen/bindings_generator_python.inc",
        "src/codegen/bindings_generator_python_module.inc",
        "src/codegen/bindings_generator_typescript.inc",
        "src/codegen/bindings_generator_typescript_module.inc",
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

# Stable, multi-language entry point: `bazel run //packages/upr_codegen:upr-gen --
# --lang {cpp,python,typescript} --input schema.upr --output out`.
alias(
    name = "upr-gen",
    actual = ":generate_bindings",
)
