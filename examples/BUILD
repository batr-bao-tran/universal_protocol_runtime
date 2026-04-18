load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//tools:upr_schema_defs.bzl", "upr_schema_library")

upr_schema_library(
    name = "market_data_schema",
    src = "market_data.upr",
    deps = ["order_types.upr"],
)

upr_schema_library(
    name = "market_data_yaml_schema",
    src = "market_data.yaml",
    deps = ["order_types.yaml"],
)

cc_binary(
    name = "upr_demo",
    srcs = ["upr_demo.cpp"],
    data = [
        ":market_data_schema",
        ":market_data_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)
