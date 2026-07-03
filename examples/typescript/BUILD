load("@aspect_rules_js//js:defs.bzl", "js_binary")

package(default_visibility = ["//visibility:public"])

# The runtime is linked at the workspace root (//:node_modules/...); Node's
# upward module resolution finds it from this package.

# TypeScript protocol bindings generated from the shared schemas by upr-gen.
# The first schema in each list is the codegen input; the rest are imported
# dependencies that must be present at their workspace-relative paths.
_TS_BINDINGS = {
    "market_data": [
        "//examples/schema:market_data.upr",
        "//examples/schema:order_types.upr",
    ],
    "advanced_market_data": ["//examples/schema:advanced_market_data.upr"],
    "hardware_telemetry": ["//examples/schema:hardware_telemetry.upr"],
}

[
    genrule(
        name = "gen_%s_ts" % schema,
        # MODULE.bazel anchors the workspace root so upr-gen can resolve
        # workspace-relative schema imports (e.g. examples/schema/*.upr).
        srcs = srcs + ["//:MODULE.bazel"],
        outs = ["generated/%s.ts" % schema],
        cmd = "$(execpath //packages/upr_codegen:generate_bindings) " +
              "--lang typescript " +
              "--input $(execpath %s) " % srcs[0] +
              "--output $@ " +
              "--runtime-import universal-protocol-runtime",
        tools = ["//packages/upr_codegen:generate_bindings"],
    )
    for schema, srcs in _TS_BINDINGS.items()
]

# Runnable examples: `bazel run //examples/typescript:<name>`. Each runs its
# TypeScript entry point directly on the Node toolchain with runtime type
# stripping; the compiled runtime is imported through the linked package.
_EXAMPLES = {
    "market_data_demo": [":generated/market_data.ts"],
    "market_data_decode_example": [":generated/advanced_market_data.ts"],
    "sensor_packet_encode_example": [":generated/hardware_telemetry.ts"],
    "hardware_byte_stream_example": [":generated/hardware_telemetry.ts"],
    "framing_and_session_example": [],
}

[
    js_binary(
        name = name,
        data = ["//:node_modules/universal-protocol-runtime"] + bindings,
        entry_point = "%s.ts" % name,
        node_options = ["--experimental-strip-types"],
    )
    for name, bindings in _EXAMPLES.items()
]
