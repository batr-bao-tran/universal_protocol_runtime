# Universal Protocol Runtime

Universal Protocol Runtime (UPR) is a C++20 library for describing binary protocols and executing them against live or replayed byte streams. It is built for systems that need explicit control over protocol layout, predictable runtime behavior, and a clean separation between schema authoring, message encoding, transport delivery, and message decoding.

## Why Use UPR

- Define a protocol once and reuse it across tools, generated bindings, and runtime decoding.
- Load human-readable schema definitions at runtime or compile them into immutable runtime metadata.
- Encode messages directly from compiled protocol metadata without introducing a separate handwritten wire-format layer.
- Decode messages as borrowed views over the original bytes instead of materializing copies by default.
- Model real protocol shapes such as repeating groups, tagged variants, presence-gated optionals, reserved gaps, and validation rules.
- Use partial decode and segmented encode paths when only selected fields or zero-copy payload attachment matter.
- Keep transport, framing, schema loading, compilation, and decoding as separate concerns so protocols remain portable across environments.
- Support dynamic runtime schema loading plus generated C++, Python, and TypeScript integrations.

## What The Repository Includes

- A schema authoring layer with `.upr` and YAML support, including runtime loading and pre-compilation
- A schema compiler that validates definitions and produces compiled protocol metadata
- A runtime for framing, encoding, partial decoding, segmented encoding, and stream polling
- Generated bindings for static C++, native-backed Python, and typed TypeScript integrations
- Protocol discovery utilities for producing draft schemas from captured samples
- Transport adapters and framed channels for file descriptors, sockets, and other byte-stream environments
- An HTML workbench for inspecting definitions, compiled protocols, discovery output, and sample frames

## How It Works

UPR follows a simple flow:

1. Write a protocol definition.
2. Load it directly at runtime for flexible decoding, or compile it into an immutable protocol description for the fastest encode and decode paths.
3. Move bytes through a transport and framing layer that matches the target environment.
4. Decode framed messages as borrowed views, or encode new messages from the same compiled protocol metadata.

At runtime, the decoder can operate on loaded schema metadata or on compiled schema metadata. Pre-compiled and generated-schema modes are the lowest-overhead options when encode or decode throughput matters most.

## Common Use Cases

Repeating groups:

```text
message Snapshot {
  level_count: uint8
  levels: Level[level_count]
}
```

Tagged variants:

```text
message Event {
  kind: uint8
  detail: variant(kind) { 1 = QuoteDetail, 2 = TradeDetail }
}
```

Presence-gated optionals:

```text
message Quote {
  presence: uint8
  note_len: uint8 present(presence, 0)
  note: utf8[note_len] present(presence, 0)
}
```

Partial decode:

```cpp
upr::DecodeFieldMask mask{};
mask.selected_fields.fill(false);
mask.selected_fields[*snapshot->find_field("levels")] = true;
mask.selected_fields[*snapshot->find_field("detail")] = true;
decoder.decode_as("Snapshot", upr::ByteSpan(frame.data(), frame.size()), &message, mask);
```

Segmented zero-copy encode:

```cpp
auto builder = encoder.build_segmented("SensorPacket", scratch);
builder->set_unsigned(SensorPacket::Fields::kSampleBytesLen, payload.size());
builder->attach_bytes(SensorPacket::Fields::kSampleBytes, payload);
builder->finalize();
```

## Example

```text
protocol market_data

enum Side: uint8 { 1 = Buy, 2 = Sell }

message Order {
  message_type: uint8 = 1
  symbol: ascii[4]
  price: float32
  quantity: uint32
  side: Side
}
```

```cpp
namespace upr = universal_protocol_runtime;

const auto definition = upr::load_protocol_definition_from_file("examples/schema/market_data.upr");
const auto compiled = upr::compile_protocol(definition.value());

upr::ProtocolDecoder decoder(compiled.value());
upr::DecodedMessage message;
const upr::DecodeStatus status =
    decoder.decode_as("Order", upr::ByteSpan(frame.data(), frame.size()), &message);
```

## Build

```bash
bazel build //...
bazel test //...
```

Run the example:

```bash
bazel run //examples:upr_demo
```

Run the split examples:

```bash
bazel run //examples:market_data_decode_example
bazel run //examples:sensor_packet_encode_example
bazel run //examples:hardware_byte_stream_example
```

## Repository Layout

- `packages/upr_authoring` contains schema loading and authoring support.
- `packages/upr_compiler` contains schema validation and compilation.
- `packages/upr_runtime` contains framing, encoding, decoding, and stream runtime support.
- `packages/upr_codegen` contains C++, Python, and TypeScript binding generators.
- `packages/upr_python` contains the Python runtime facade, framing helpers, and native-backed generated module support.
- `packages/upr_typescript` contains the TypeScript/JavaScript runtime, typed facades, and framing helpers.
- `packages/upr_discovery` contains protocol discovery utilities.
- `packages/upr_adapters` contains concrete transport adapters and framed channel support.
- `packages/upr_workbench` contains HTML inspection tooling for definitions, compiled protocols, discovery output, and sample frames.
- `packages/universal_protocol_runtime` provides the umbrella public interface.
- `examples` contains runnable C++, Python, and TypeScript examples (`examples/cpp`, `examples/python`, `examples/typescript`) driven by the shared schemas in `examples/schema`.

## Further Reading

[DESIGN.md](DESIGN.md) describes the high-level architecture and design goals.
[schema/README.md](schema/README.md) contains the schema authoring instructions and supported constructs.
