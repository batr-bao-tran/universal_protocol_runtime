# Universal Protocol Runtime

Universal Protocol Runtime (UPR) is a C++20 library for describing binary protocols and executing them against live or replayed byte streams. It is built for systems that need explicit control over protocol layout, predictable runtime behavior, and a clean separation between schema authoring and message decoding.

## Why Use UPR

- Define a protocol once and reuse it across tools, generated bindings, and runtime decoding.
- Load human-readable schema definitions at runtime or compile them into immutable runtime metadata.
- Decode messages as borrowed views over the original bytes instead of materializing copies by default.
- Keep transport, framing, schema loading, compilation, and decoding as separate concerns so protocols remain portable across environments.
- Support both dynamic runtime schema loading and static integration (C++ and Python bindings).

## What The Repository Includes

- A schema authoring layer with `.upr` and YAML support, including runtime loading and pre-compilation
- A schema compiler that validates definitions and produces compiled protocol metadata
- A runtime for framing, decoding, and stream polling
- Generated bindings for static C++ and Python integrations
- Protocol discovery utilities for producing draft schemas from captured samples
- POSIX transport adapters for file descriptors and sockets
- An HTML workbench for inspection and review

## How It Works

UPR follows a simple flow:

1. Write a protocol definition.
2. Load it directly at runtime for flexible decoding, or compile it into an immutable protocol description for the fastest path.
3. Feed framed bytes into the decoder.
4. Read decoded fields from borrowed message views.

At runtime, the decoder can operate on loaded schema metadata or on compiled schema metadata. Pre-compiled and generated-schema modes are the lowest-overhead options when decode throughput matters most.

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

const auto definition = upr::load_protocol_definition_from_file("examples/market_data.upr");
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

## Repository Layout

- `packages/upr_authoring` contains schema loading and authoring support.
- `packages/upr_compiler` contains schema validation and compilation.
- `packages/upr_runtime` contains framing, decoding, and stream runtime support.
- `packages/upr_codegen` contains generated binding support.
- `packages/upr_discovery` contains protocol discovery utilities.
- `packages/upr_adapters` contains concrete transport adapters.
- `packages/upr_workbench` contains HTML inspection tooling.
- `packages/universal_protocol_runtime` provides the umbrella public interface.

## Further Reading

[DESIGN.md](DESIGN.md) describes the high-level architecture and design goals.
[schema/README.md](schema/README.md) contains the schema authoring instructions and supported constructs.
