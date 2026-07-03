load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "upr_demo",
    srcs = ["src/upr_demo.cpp"],
    data = [
        "//examples/schema:market_data_schema",
        "//examples/schema:market_data_yaml_schema",
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
        "//examples/schema:advanced_market_data_schema",
        "//examples/schema:advanced_market_data_yaml_schema",
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
        "//examples/schema:hardware_telemetry_schema",
        "//examples/schema:hardware_telemetry_yaml_schema",
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
        "//examples/schema:hardware_telemetry_schema",
        "//examples/schema:hardware_telemetry_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
    ],
)

cc_binary(
    name = "network_transport_example",
    srcs = [
        "include/hardware_telemetry_example_support.hpp",
        "src/network_transport_example.cpp",
    ],
    data = [
        "//examples/schema:hardware_telemetry_schema",
        "//examples/schema:hardware_telemetry_yaml_schema",
    ],
    deps = [
        "//packages/universal_protocol_runtime",
        "//packages/upr_adapters",
    ],
)
