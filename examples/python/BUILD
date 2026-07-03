load("@rules_python//python:py_binary.bzl", "py_binary")
load("//tools:upr_python_defs.bzl", "upr_python_bindings")

package(default_visibility = ["//visibility:public"])

# Native-backed protocol bindings generated from the shared schemas. Each target
# emits the `generated.<module>` facade and compiles its pybind11 codec, so the
# examples import a module whose CODEC is always native-backed.
upr_python_bindings(
    name = "market_data_bindings",
    module_name = "market_data",
    schema = "//examples/schema:market_data.upr",
    schema_deps = ["//examples/schema:order_types.upr"],
)

upr_python_bindings(
    name = "advanced_market_data_bindings",
    module_name = "advanced_market_data",
    schema = "//examples/schema:advanced_market_data.upr",
)

upr_python_bindings(
    name = "hardware_telemetry_bindings",
    module_name = "hardware_telemetry",
    schema = "//examples/schema:hardware_telemetry.upr",
)

# Runnable examples: `bazel run //examples/python:<name>`. The value is the
# binding the example imports, or None when it uses only the runtime.
_RUNTIME = "//packages/upr_python:universal_protocol_runtime"

_EXAMPLES = {
    "market_data_demo": ":market_data_bindings",
    "market_data_decode_example": ":advanced_market_data_bindings",
    "sensor_packet_encode_example": ":hardware_telemetry_bindings",
    "hardware_byte_stream_example": ":hardware_telemetry_bindings",
    "framing_and_session_example": None,
}

[
    py_binary(
        name = name,
        srcs = ["%s.py" % name],
        deps = [binding] if binding else [_RUNTIME],
    )
    for name, binding in _EXAMPLES.items()
]
