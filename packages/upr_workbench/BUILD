load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_workbench",
    srcs = [
        "src/workbench/html_workbench.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_authoring",
        "//packages/upr_compiler",
        "//packages/upr_core",
        "//packages/upr_discovery",
        "//packages/utils",
    ],
)
