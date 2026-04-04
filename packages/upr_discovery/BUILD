load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_discovery",
    srcs = [
        "src/discovery/protocol_discovery.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_authoring",
        "//packages/upr_compiler",
        "//packages/upr_core",
        "//packages/utils",
    ],
)
