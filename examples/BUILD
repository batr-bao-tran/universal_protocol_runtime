load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "upr_demo",
    srcs = ["upr_demo.cpp"],
    data = ["market_data.yaml"],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)
