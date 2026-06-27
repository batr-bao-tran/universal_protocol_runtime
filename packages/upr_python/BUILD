load("@rules_python//python:py_library.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

exports_files([
    "pyproject.toml",
    "README.md",
])

py_library(
    name = "universal_protocol_runtime",
    srcs = glob(["src/universal_protocol_runtime/*.py"]),
    data = ["src/universal_protocol_runtime/py.typed"],
    imports = ["src"],
)
