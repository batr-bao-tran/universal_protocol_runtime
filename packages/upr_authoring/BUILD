load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_authoring",
    srcs = [
        "src/pdl/yaml_loader.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_core",
        "//packages/utils",
        "@yaml-cpp//:yaml-cpp",
    ],
)
