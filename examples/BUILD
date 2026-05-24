load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//tools:upr_schema_defs.bzl", "upr_schema_library")

upr_schema_library(
    name = "market_data_schema",
    src = "schema/market_data.upr",
    deps = ["schema/order_types.upr"],
)

upr_schema_library(
    name = "market_data_yaml_schema",
    src = "schema/market_data.yaml",
    deps = ["schema/order_types.yaml"],
)

upr_schema_library(
    name = "advanced_market_data_schema",
    src = "schema/advanced_market_data.upr",
)

upr_schema_library(
    name = "advanced_market_data_yaml_schema",
    src = "schema/advanced_market_data.yaml",
)

upr_schema_library(
    name = "hardware_telemetry_schema",
    src = "schema/hardware_telemetry.upr",
)

upr_schema_library(
    name = "hardware_telemetry_yaml_schema",
    src = "schema/hardware_telemetry.yaml",
)

cc_binary(
    name = "upr_demo",
    srcs = ["src/upr_demo.cpp"],
    data = [
        ":market_data_schema",
        ":market_data_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)

cc_binary(
    name = "market_data_decode_example",
    srcs = [
        "include/advanced_market_data_example_support.hpp",
        "src/market_data_decode_example.cpp",
    ],
    data = [
        ":advanced_market_data_schema",
        ":advanced_market_data_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)

cc_binary(
    name = "sensor_packet_encode_example",
    srcs = [
        "include/hardware_telemetry_example_support.hpp",
        "src/sensor_packet_encode_example.cpp",
    ],
    data = [
        ":hardware_telemetry_schema",
        ":hardware_telemetry_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)

cc_binary(
    name = "hardware_byte_stream_example",
    srcs = [
        "include/hardware_telemetry_example_support.hpp",
        "src/hardware_byte_stream_example.cpp",
    ],
    data = [
        ":hardware_telemetry_schema",
        ":hardware_telemetry_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)
