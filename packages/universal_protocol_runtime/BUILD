load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "universal_protocol_runtime",
    hdrs = ["include/universal_protocol_runtime/universal_protocol_runtime.hpp"],
    includes = ["include"],
    deps = [
        "//packages/upr_adapters",
        "//packages/upr_authoring",
        "//packages/upr_codegen",
        "//packages/upr_compiler",
        "//packages/upr_core",
        "//packages/upr_discovery",
        "//packages/upr_runtime",
        "//packages/upr_workbench",
        "//packages/utils",
    ],
)
