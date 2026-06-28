# Universal Protocol Runtime Design

Universal Protocol Runtime is designed around a straightforward principle: binary protocols should be defined in a form that is easy to understand, validated before execution, and efficient to use at runtime.

The project is split into distinct layers so that protocol authoring, compilation, encoding, transport, and runtime execution remain clear, composable, and independently reusable.

## Design Goals

- Keep protocol definitions readable and explicit.
- Separate authoring concerns from runtime concerns.
- Support runtime schema loading for flexible use cases, and compilation for the lowest-overhead encode and decode paths.
- Expose decoded data without forcing unnecessary copying.
- Cover production protocol shapes such as collections, tagged unions, sparse optional fields, alignment gaps, and validation rules.
- Allow selective decode and segmented encode paths when performance-sensitive applications do not need full-message materialization.
- Allow the same protocol to be used across live or offline streams, tests, and integrations.

## Architectural Model

UPR is built as three primary layers:

1. Authoring
2. Compilation
3. Runtime

Authoring is where protocol definitions are written and loaded. Definitions can be consumed directly at runtime for flexible schema-driven decoding, or compiled into a stable protocol description for repeated and lower-overhead encode and decode execution. Runtime uses whichever form is appropriate for the encoder or decoder mode in use.

## Layer Responsibilities

### Authoring

The authoring layer is responsible for the human-facing description of a protocol. It supports concise schema definitions for scalars, strings, bytes, structs, collections, tagged variants, presence-gated fields, reserved ranges, alignment hints, checksums, and validation rules, and provides the entry point for turning those definitions into in-memory protocol models.

### Compilation

The compiler mode allows running execution with maximum performance. It validates protocol definitions, resolves dependencies such as size/count/tag/presence fields, and produces compiled protocol metadata or generated bindings for C++, native-backed Python, and TypeScript integrations.

### Runtime

The runtime layer receives data from transports, applies framing, encodes or decodes messages against loaded or compiled protocol descriptions, and exposes the result to the caller.

Given framed bytes and protocol metadata, it decodes messages consistently and efficiently, or builds encoded messages directly from compiled layouts, with performance as top priority (throughput and overhead). It also supports field masks for partial decode and segmented builders for zero-copy payload attachment.

## Supporting Layers

Several repository packages build on top of the core layers:

- Discovery helps infer draft protocol definitions from captured samples.
- Code generation produces static C++ bindings, pybind-backed Python modules, and typed TypeScript facades from compiled protocols.
- Adapters connect the runtime to concrete transport environments and provide framed channel support over byte streams.
- The workbench provides an HTML inspection surface for definitions, compiled protocols, discovery output, and sample data.

## Repository Structure

- `packages/upr_core` contains shared foundational types and utilities.
- `packages/upr_authoring` contains protocol authoring and schema loading.
- `packages/upr_compiler` contains validation and compilation.
- `packages/upr_runtime` contains framing, encoding, decoding, and stream runtime support.
- `packages/upr_discovery` contains protocol discovery support.
- `packages/upr_codegen` contains C++, Python, and TypeScript binding generators.
- `packages/upr_python` contains the Python runtime facade, framing helpers, and native-backed generated module support.
- `packages/upr_typescript` contains the TypeScript/JavaScript runtime, typed facades, and framing helpers.
- `packages/upr_adapters` contains transport adapters and framed transport helpers.
- `packages/upr_workbench` contains HTML inspection and review tooling.
- `packages/universal_protocol_runtime` provides the umbrella public interface.

## Runtime View

From the runtime perspective, the model is small:

1. Read bytes from a transport.
2. Identify frame boundaries.
3. Decode a frame using loaded schema metadata or a compiled protocol, or encode a message from compiled protocol metadata.
4. Return a decoded message view or encoded bytes for transport.

That is the core execution path. Everything else in the repository exists to support, prepare, or extend that flow. Definitions can be loaded directly when flexibility matters, or compiled and reused wherever encode or decode throughput matters most.

## Common Patterns

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
decoder.decode_as("Snapshot", upr::ByteSpan(frame.data(), frame.size()), &message, mask);
```

Segmented encode:

```cpp
auto builder = encoder.build_segmented("SensorPacket", scratch);
builder->set_unsigned(SensorPacket::Fields::kSampleBytesLen, payload.size());
builder->attach_bytes(SensorPacket::Fields::kSampleBytes, payload);
builder->finalize();
```
