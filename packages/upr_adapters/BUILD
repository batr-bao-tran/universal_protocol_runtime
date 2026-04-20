load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "upr_adapters",
    srcs = [
        "src/adapters/byte_stream_transport.cpp",
        "src/adapters/frame_channel.cpp",
        "src/adapters/io_uring_reactor.cpp",
        "src/adapters/local_shm_ring_transport.cpp",
        "src/adapters/pluggable_stream_engine.cpp",
        "src/adapters/posix_fd_transport.cpp",
        "src/adapters/posix_socket_transport.cpp",
        "src/adapters/tcp_stream_transport.cpp",
        "src/adapters/unix_socket_transport.cpp",
        "src/adapters/upr_session.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]) + ["src/adapters/stream_transport_support.hpp"],
    includes = ["include"],
    deps = [
        "//packages/upr_runtime",
        "//packages/utils",
        "@liburing",
    ],
)

cc_binary(
    name = "upr_transport_benchmark",
    srcs = ["benchmarks/upr_transport_benchmark.cpp"],
    deps = [
        ":upr_adapters",
        "@google_benchmark//:benchmark",
    ],
)
