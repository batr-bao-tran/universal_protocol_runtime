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
        "src/encoder/message_encoder.cpp",
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

# Header-only view of the direct encode/decode support headers. Consumers of
# generated bindings can depend on this target (plus -DUPR_RUNTIME_SCALAR_ONLY)
# to get the fast paths without linking simdutf / Highway / crc32c.
cc_library(
    name = "upr_runtime_scalar_headers",
    hdrs = [
        "include/universal_protocol_runtime/decoder/detail/runtime_byte_ops.hpp",
        "include/universal_protocol_runtime/decoder/direct_decode_support.hpp",
        "include/universal_protocol_runtime/encoder/direct_encode_support.hpp",
        "include/universal_protocol_runtime/encoder/encode_status.hpp",
    ],
    includes = ["include"],
    deps = [
        "//packages/upr_core",
    ],
)

cc_test(
    name = "scalar_only_runtime_test",
    size = "small",
    srcs = ["tests/scalar_only_runtime_test.cpp"],
    local_defines = ["UPR_RUNTIME_SCALAR_ONLY=1"],
    deps = [
        ":upr_runtime_scalar_headers",
        "@googletest//:gtest_main",
    ],
)

genrule(
    name = "general_direct_codec_generated",
    srcs = ["tests/general_direct_codec.upr"],
    outs = ["tests/upr_general_direct_generated.hpp"],
    cmd = "$(location //packages/upr_codegen:generate_bindings)" +
          " --input $(location tests/general_direct_codec.upr)" +
          " --output $@" +
          " --namespace-prefix universal_protocol_runtime" +
          " --protocol-namespace gdc" +
          " --header-guard UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_TESTS__UPR_GENERAL_DIRECT_GENERATED_HPP_",
    tools = ["//packages/upr_codegen:generate_bindings"],
)

cc_test(
    name = "general_direct_codec_test",
    size = "small",
    srcs = [
        "tests/general_direct_codec_test.cpp",
        ":general_direct_codec_generated",
    ],
    data = ["tests/general_direct_codec.upr"],
    deps = [
        ":upr_runtime",
        "//packages/upr_authoring",
        "//packages/upr_compiler",
        "@googletest//:gtest_main",
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
        ":benchmark_generated_header",
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

genrule(
    name = "benchmark_generated_header",
    srcs = ["benchmarks/benchmark_protocol.upr"],
    outs = ["benchmarks/upr_benchmark_generated.hpp"],
    cmd = "$(location //packages/upr_codegen:generate_bindings)" +
          " --input $(location benchmarks/benchmark_protocol.upr)" +
          " --output $@" +
          " --namespace-prefix universal_protocol_runtime" +
          " --protocol-namespace benchmarks::generated" +
          " --header-guard UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__UPR_BENCHMARK_GENERATED_HPP_",
    tools = ["//packages/upr_codegen:generate_bindings"],
)

cc_test(
    name = "benchmark_generated_header_test",
    size = "small",
    srcs = ["benchmarks/benchmark_generated_header_test.cpp"],
    data = [
        "benchmarks/benchmark_protocol.upr",
        ":benchmark_generated_header",
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
    data = ["benchmarks/benchmark_protocol.upr"],
    deps = [
        ":protocol_benchmark_support",
        "@google_benchmark//:benchmark",
    ],
)
