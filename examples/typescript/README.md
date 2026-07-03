# TypeScript examples

These examples use the dependency-free TypeScript runtime with protocol modules
generated from the shared schemas in [`../schema`](../schema). The generated
modules expose typed interfaces and encode/decode helpers for each message.

The TypeScript runtime is a dependency-free `Codec` plus framing/session helpers
(see [`packages/upr_typescript`](../../packages/upr_typescript)). It does not
provide socket transports; use normal Node.js stream/socket APIs and pass
received `Uint8Array` chunks to `FrameDecoder`.

## Layout

| File | Features shown |
| --- | --- |
| `market_data_demo.ts` | encode + fixed-size frame decode, enums, `ascii[N]`, `float32`, typed interfaces |
| `market_data_decode_example.ts` | repeating groups, tagged variants, presence-gated optionals |
| `sensor_packet_encode_example.ts` | derived length fields, reserved + alignment gaps, dynamic `bytes`, validation invariant |
| `hardware_byte_stream_example.ts` | length-prefixed framing + streaming `FrameDecoder` |
| `framing_and_session_example.ts` | `encodeFrame`/`tryReadFrame`/`iterFrames`/`FrameDecoder`, `UPR1` handshake, checksums |

## Run

The examples are built and run entirely by Bazel. The protocol bindings are
generated from the shared schemas by `upr-gen`, the runtime is compiled by
`rules_ts`, and each example runs on the Bazel-managed Node toolchain with
runtime type stripping:

```bash
bazel run //examples/typescript:market_data_demo
bazel run //examples/typescript:market_data_decode_example
bazel run //examples/typescript:sensor_packet_encode_example
bazel run //examples/typescript:hardware_byte_stream_example
bazel run //examples/typescript:framing_and_session_example
```

Build them all without running: `bazel build //examples/typescript:all`.

## Generated bindings

Each example imports a protocol module produced from a schema by the
`gen_<schema>_ts` genrules in [`BUILD`](BUILD). To inspect one by hand:

```bash
bazel run //packages/upr_codegen:upr-gen -- \
    --lang typescript \
    --input examples/schema/advanced_market_data.upr \
    --output /tmp/advanced_market_data.ts \
    --runtime-import universal-protocol-runtime
```

Wide integers (7- and 8-byte fields, e.g. `TradeDetail.trade_id`) decode to
`bigint`; narrower integers and floats decode to `number`.
