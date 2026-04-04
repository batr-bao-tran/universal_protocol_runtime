load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_adapters",
    srcs = [
        "src/adapters/posix_fd_transport.cpp",
        "src/adapters/posix_socket_transport.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_runtime",
        "//packages/utils",
    ],
)
