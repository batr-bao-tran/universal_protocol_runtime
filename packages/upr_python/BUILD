load("@rules_python//python:packaging.bzl", "py_wheel")
load("@rules_python//python:py_library.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "universal_protocol_runtime",
    srcs = glob(["src/universal_protocol_runtime/*.py"]),
    data = ["src/universal_protocol_runtime/py.typed"],
    imports = ["src"],
)

filegroup(
    name = "py_typed_marker",
    srcs = ["src/universal_protocol_runtime/py.typed"],
)

# Pure-Python wheel built entirely by Bazel (no setuptools invocation) and the
# single source of truth for the published package metadata. Produces
# universal_protocol_runtime-<version>-py3-none-any.whl.
py_wheel(
    name = "wheel",
    abi = "none",
    author = "Universal Protocol Runtime",
    classifiers = [
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Typing :: Typed",
    ],
    description_file = "README.md",
    distribution = "universal-protocol-runtime",
    homepage = "https://github.com/universal-protocol-runtime/universal_protocol_runtime",
    license = "Apache-2.0",
    platform = "any",
    python_requires = ">=3.9",
    python_tag = "py3",
    # Strip the Bazel source layout prefix so the wheel exposes the top-level
    # `universal_protocol_runtime` package.
    strip_path_prefixes = ["packages/upr_python/src"],
    summary = "Pure-Python encoder/decoder, framing and session helpers for the Universal Protocol Runtime",
    version = "0.1.0",
    deps = [
        ":universal_protocol_runtime",
        # PEP 561 marker: py_wheel only packages deps' direct files, not the
        # py_library `data`, so include it explicitly.
        ":py_typed_marker",
    ],
)
