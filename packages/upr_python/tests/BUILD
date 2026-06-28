load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")
load("@python_deps//:requirements.bzl", "requirement")
load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_test.bzl", "py_test")

package(default_visibility = ["//visibility:private"])

genrule(
    name = "generated_general_direct_codec",
    srcs = ["general_direct_codec.upr"],
    outs = [
        "generated_general_direct_codec.py",
        "generated_general_direct_codec_native.cpp",
        "generated_general_direct_codec_native.hpp",
    ],
    cmd = "$(location //packages/upr_codegen:generate_bindings)" +
          " --lang python" +
          " --input $(location general_direct_codec.upr)" +
          " --output $(@D)/generated_general_direct_codec.py" +
          " --native-output $(@D)/generated_general_direct_codec_native.cpp" +
          " --native-header-output $(@D)/generated_general_direct_codec_native.hpp" +
          " --native-header-include packages/upr_python/tests/generated_general_direct_codec_native.hpp" +
          " --module-name general_direct_codec",
    tools = ["//packages/upr_codegen:generate_bindings"],
)

pybind_extension(
    name = "_general_direct_codec_native",
    srcs = [
        ":generated_general_direct_codec_native.cpp",
        ":generated_general_direct_codec_native.hpp",
    ],
    copts = ["-std=c++20"],
    deps = [
        "//packages/upr_runtime",
    ],
)

py_test(
    name = "python_codec_test",
    size = "small",
    srcs = [
        "python_codec_test.py",
        ":generated_general_direct_codec.py",
    ],
    imports = ["."],
    main = "python_codec_test.py",
    # The pybind11 extension is built with -fsanitize=address under the
    # sanitize config, but it is dlopen'd into a non-instrumented Python
    # interpreter that never links the ASan runtime. The underlying C++
    # runtime is already covered by the C++ sanitizer tests, so the sanitizer
    # suite skips targets tagged "no-sanitize".
    tags = ["no-sanitize"],
    deps = [
        ":_general_direct_codec_native",
        "//packages/upr_python:universal_protocol_runtime",
        requirement("pytest"),
    ],
)

py_binary(
    name = "python_codec_coverage",
    srcs = [
        "python_codec_test.py",
        "python_coverage_main.py",
        ":generated_general_direct_codec.py",
    ],
    imports = ["."],
    main = "python_coverage_main.py",
    # Must match python_codec_test's tags so the shared PyCompile action for the
    # common .py sources stays a single action.
    tags = ["no-sanitize"],
    deps = [
        ":_general_direct_codec_native",
        "//packages/upr_python:universal_protocol_runtime",
        requirement("coverage"),
        requirement("pytest"),
    ],
)
