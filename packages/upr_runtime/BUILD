load("@flatbuffers//:build_defs.bzl", "flatbuffer_cc_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_runtime",
    srcs = [
        "src/decoder/decoded_message.cpp",
        "src/decoder/protocol_decoder.cpp",
        "src/decoder/runtime_byte_ops.cpp",
        "src/framing/fixed_size_framer.cpp",
        "src/framing/length_prefixed_framer.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//packages/upr_compiler",
        "//packages/upr_core",
        "//packages/utils",
        "@crc32c",
        "@highway//:hwy",
        "@simdutf",
    ],
)

proto_library(
    name = "benchmark_messages_proto",
    srcs = ["benchmarks/benchmark_messages.proto"],
)

cc_proto_library(
    name = "benchmark_messages_cc_proto",
    deps = [":benchmark_messages_proto"],
)

flatbuffer_cc_library(
    name = "benchmark_flatbuffers_cc",
    srcs = [
        "benchmarks/benchmark_blob_envelope.fbs",
        "benchmarks/benchmark_market_data.fbs",
    ],
)

cc_library(
    name = "protocol_benchmark_support",
    srcs = ["benchmarks/protocol_benchmark_support.cpp"],
    hdrs = [
        "benchmarks/protocol_benchmark_support.hpp",
        "benchmarks/upr_benchmark_generated.hpp",
    ],
    deps = [
        ":benchmark_flatbuffers_cc",
        ":benchmark_messages_cc_proto",
        ":upr_runtime",
        "//packages/upr_authoring",
        "//packages/upr_compiler",
        "@flatbuffers",
        "@protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "benchmark_generated_header_test",
    srcs = ["benchmarks/benchmark_generated_header_test.cpp"],
    data = [
        "benchmarks/benchmark_protocol.yaml",
        "benchmarks/upr_benchmark_generated.hpp",
    ],
    deps = [
        "//packages/upr_authoring",
        "//packages/upr_codegen",
        "//packages/upr_compiler",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "runtime_benchmark",
    srcs = ["benchmarks/runtime_benchmark.cpp"],
    data = ["benchmarks/benchmark_protocol.yaml"],
    deps = [
        ":protocol_benchmark_support",
        "@google_benchmark//:benchmark",
    ],
)
