load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_library(
    name = "test_support",
    hdrs = ["detail/test_support.hpp"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)

cc_tests_from_glob(
    size = "small",
    default_deps = [
        ":test_support",
        "//packages/universal_protocol_runtime:universal_protocol_runtime",
        "@googletest//:gtest_main",
    ],
)
