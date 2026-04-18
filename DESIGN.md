# Universal Protocol Runtime Design

Universal Protocol Runtime is designed around a straightforward principle: binary protocols should be defined in a form that is easy to understand, validated before execution, and efficient to use at runtime.

The project is split into distinct layers so that protocol authoring, compilation, and runtime execution remain clear, composable, and independently reusable.

## Design Goals

- Keep protocol definitions readable and explicit.
- Separate authoring concerns from runtime concerns.
- Support runtime schema loading for flexible use cases, and compilation for the lowest-overhead decode path.
- Expose decoded data without forcing unnecessary copying.
- Allow the same protocol to be used across live streams, replay workflows, tests, and generated integrations.
- Make tooling such as discovery, code generation, adapters, and inspection additive rather than foundational.

## Architectural Model

UPR is built as three primary layers:

1. Authoring
2. Compilation
3. Runtime

Authoring is where protocol definitions are written and loaded. Definitions can be consumed directly at runtime for flexible schema-driven decoding, or compiled into a stable protocol description for repeated and lower-overhead execution. Runtime uses whichever form is appropriate for the decoder mode in use.

## Layer Responsibilities

### Authoring

The authoring layer is responsible for the human-facing description of a protocol. It supports concise schema definitions and provides the entry point for turning those definitions into in-memory protocol models.

### Compilation

The compiler mode allows running the execution with maximum performance. It validates protocol definitions and produces a compiled protocol representation that can be reused across decoders, streams, and integrations, in C++ or Python.

### Runtime

The runtime layer receives data from transports, applies framing, decodes messages against loaded or compiled protocol descriptions, and exposes decoded views to the caller.

Given framed bytes and protocol metadata, it decodes messages consistently and efficiently, with performance as top priority (throughput and overhead).

## Supporting Layers

Several repository packages build on top of the core layers:

- Discovery helps infer draft protocol definitions from captured samples.
- Code generation produces static bindings from compiled protocols.
- Adapters connect the runtime to concrete transport environments.
- The workbench provides an inspection surface for definitions, compiled protocols, and sample data.

## Repository Structure

- `packages/upr_core` contains shared foundational types and utilities.
- `packages/upr_authoring` contains protocol authoring and schema loading.
- `packages/upr_compiler` contains validation and compilation.
- `packages/upr_runtime` contains framing, decoding, and stream runtime support.
- `packages/upr_discovery` contains protocol discovery support.
- `packages/upr_codegen` contains generated binding support.
- `packages/upr_adapters` contains transport adapters.
- `packages/upr_workbench` contains inspection and review tooling.
- `packages/universal_protocol_runtime` provides the umbrella public interface.

## Runtime View

From the runtime perspective, the model is small:

1. Read bytes from a transport.
2. Identify frame boundaries.
3. Decode a frame using loaded schema metadata or a compiled protocol.
4. Return a decoded message view.

That is the core execution path. Everything else in the repository exists to support, prepare, or extend that flow. Definitions can be loaded directly when flexibility matters, or compiled and reused wherever decoding throughput matters most.
