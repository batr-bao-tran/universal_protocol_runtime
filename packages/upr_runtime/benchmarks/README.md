# Protocol Benchmark

This benchmark compares UPR decode modes and practical external baselines on the same logical messages and validation work:

- `upr_reflective`: generic schema-driven decode plus reflective field lookup
- `upr_resolved_ids`: generic decode with pre-resolved message layout and field ids
- `upr_static_schema`: generated typed C++ decode with the direct zero-copy static-schema fast path where supported, and generic fallback otherwise
- `packed_binary`: a hand-written parser over the same compact wire layout as UPR
- `protobuf_lite`: generated Protocol Buffers C++ with the lite runtime
- `flatbuffers`: generated FlatBuffers C++ with verifier enabled

The benchmark binary also includes byte-span microbenchmarks for the scalar helpers and the runtime-dispatched large-span helpers used for ASCII and UTF-8 validation plus built-in checksum scans. Those runtime helpers use the same UPR API while routing large spans through `simdutf`, `crc32c`, and Highway-backed kernels and preserving the scalar constexpr implementations for compile-time specialization and small-buffer fallback.

## Scope

- Measures decode, validation, and field extraction CPU cost over prebuilt corpora
- Does not measure encoding, transport latency, RPC stacks, or disk I/O
- Uses the same logical messages across formats
- Keeps the same outer `uint16` little-endian frame prefix for all formats
- Includes validation work instead of benchmarking unchecked parsing

Validation in the measured path:

- UPR: schema checks plus checksum validation
- `packed_binary`: bounds checks plus checksum validation
- `protobuf_lite`: message parsing and field extraction
- `flatbuffers`: buffer verification plus field extraction

## Workloads

- `blob_small`: `32` payload bytes, short fixed frames where per-message overhead dominates
- `blob_large`: `2048` payload bytes, payload traversal dominates
- `market_data`: fixed-width scalar-heavy message with symbol bytes and checksum

All corpora use `16` deterministic seeds so the results are not driven by one payload pattern.

## Methodology

- Harness: Google Benchmark
- Build: `bazel build -c opt //packages/upr_runtime:runtime_benchmark`
- Pinning used for measurement: `taskset -c 0`
- Timing mode: process CPU time
- Warmup per benchmark: `0.20 s`
- Minimum measured time per repetition: `0.75 s`
- Repetitions per process run: `12`
- Full decode suite: `4` independent pinned process runs, `48` timed repetitions per benchmark
- Byte-op suite: `4` independent pinned process runs, `48` timed repetitions per benchmark
- Primary metric: mean of each run's median CPU time
- Uncertainty shown below: approximate `95%` confidence interval across independent process runs

Google Benchmark reported two host-level caveats during collection:

- CPU scaling was enabled
- ASLR was enabled

As a result, sub-`1%` gaps should be treated as noise. The larger differences called out below are materially larger than the observed run-to-run spread.

## Test Environment

- Date: `2026-04-05`
- Kernel: `Linux 6.17.0-19-generic x86_64 GNU/Linux`
- CPU: `13th Gen Intel(R) Core(TM) i9-13950HX`
- Logical CPUs: `32`

## End-To-End Decode Results

### `blob_small`

| Protocol | Mean CPU us | 95% CI | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 2006.0 | +/- 27.7 | 36.00 |
| `packed_binary` | 2776.0 | +/- 32.1 | 36.00 |
| `flatbuffers` | 3667.6 | +/- 84.3 | 63.98 |
| `protobuf_lite` | 6834.2 | +/- 104.6 | 38.49 |
| `upr_resolved_ids` | 8282.1 | +/- 89.8 | 36.00 |
| `upr_reflective` | 12758.0 | +/- 3250.4 | 36.00 |

Read:

- `upr_static_schema` is the fastest path on tiny frames in this benchmark, about `1.38x` faster than `packed_binary`.
- `flatbuffers` is faster than the dynamic UPR modes, but it carries materially larger messages here, about `1.78x` the encoded size of UPR and `packed_binary`.
- `upr_reflective` has one slow outlier run in this scenario, which widens its confidence interval. That changes the exact number more than the ranking: reflective decode remains much slower than the static-schema path on short messages.

### `blob_large`

| Protocol | Mean CPU us | 95% CI | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 11501.2 | +/- 27.3 | 2052.00 |
| `upr_resolved_ids` | 11933.8 | +/- 7.8 | 2052.00 |
| `upr_reflective` | 12194.6 | +/- 26.4 | 2052.00 |
| `packed_binary` | 13961.8 | +/- 19.3 | 2052.00 |
| `flatbuffers` | 13985.4 | +/- 52.4 | 2079.98 |
| `protobuf_lite` | 14434.1 | +/- 31.4 | 2055.49 |

Read:

- Large payloads compress most framework overhead, so every implementation moves closer together.
- The UPR modes remain ahead of the external baselines in this harness, but the practical story is different from `blob_small`: `upr_static_schema` is only about `1.21x` faster than `packed_binary`.
- `packed_binary` and `flatbuffers` are effectively tied here once the confidence intervals are considered.

### `market_data`

| Protocol | Mean CPU us | 95% CI | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 6252.1 | +/- 60.7 | 64.00 |
| `packed_binary` | 13587.2 | +/- 74.0 | 64.00 |
| `flatbuffers` | 15043.6 | +/- 128.3 | 120.00 |
| `upr_resolved_ids` | 17188.3 | +/- 108.7 | 64.00 |
| `protobuf_lite` | 19527.6 | +/- 188.1 | 73.41 |
| `upr_reflective` | 29982.9 | +/- 398.8 | 64.00 |

Read:

- `upr_static_schema` is the strongest result in this suite for scalar-heavy fixed layouts, about `2.17x` faster than `packed_binary`.
- `flatbuffers` is the strongest non-UPR generated baseline in this scenario, but it nearly doubles the encoded size relative to UPR and `packed_binary`.
- The dynamic UPR modes are much slower than the generated path because they use the generic decode-and-access model.

## Byte-Span And Runtime Backend Results

These microbenchmarks isolate the large-span operations handled by the runtime backend.

| Operation | Size (B) | Scalar mean us | Runtime mean us | Runtime speedup |
| --- | ---: | ---: | ---: | ---: |
| ASCII | 256 | 0.086 | 0.007 | 12.29x |
| ASCII | 4096 | 1.110 | 0.049 | 22.65x |
| ASCII | 65536 | 17.400 | 0.888 | 19.59x |
| UTF-8 | 256 | 0.273 | 0.029 | 9.41x |
| UTF-8 | 4096 | 4.200 | 0.329 | 12.77x |
| UTF-8 | 65536 | 68.500 | 5.140 | 13.33x |
| XOR-8 checksum | 256 | 0.063 | 0.014 | 4.50x |
| XOR-8 checksum | 4096 | 1.090 | 0.058 | 18.79x |
| XOR-8 checksum | 65536 | 17.400 | 0.859 | 20.26x |
| SUM16 checksum | 256 | 0.072 | 0.011 | 6.55x |
| SUM16 checksum | 4096 | 1.090 | 0.099 | 11.01x |
| SUM16 checksum | 65536 | 17.500 | 1.430 | 12.24x |
| CRC32C | 256 | 0.511 | 0.014 | 36.50x |
| CRC32C | 4096 | 8.660 | 0.146 | 59.32x |
| CRC32C | 65536 | 139.000 | 2.210 | 62.90x |

Read:

- The runtime backend materially improves every large-span primitive it targets, with especially large gains for UTF-8 validation and CRC32C.
- Those gains are real and stable, but they do not erase the higher-level cost of generic reflective decode on tiny or scalar-heavy messages.
- The largest end-to-end wins still come from combining the direct generated path with compact wire layouts and zero-copy field access, not from byte-span acceleration alone.

## Focused UPR Decode Snapshot

A focused measurement over the three UPR modes on the benchmark host shows the same runtime characteristics in the end-to-end path:

- `upr_static_schema/blob_small`: `2014 us`
- `upr_static_schema/blob_large`: `11575 us`
- `upr_static_schema/market_data`: `4498 us`

These measurements complement the full cross-protocol tables above by isolating the static-schema UPR path on the same workloads.

## Runtime Backend Layout

- `simdutf` backs runtime ASCII and UTF-8 validation. In this benchmark matrix, those paths measure roughly `9.4x` to `22.6x` faster than the scalar implementations, depending on span type and size.
- `crc32c` backs runtime CRC32C calculation. In this benchmark matrix, it is the largest single backend acceleration, at roughly `36x` to `63x` over the scalar implementation.
- Highway backs the generic runtime XOR-8 and SUM16 kernels. In this benchmark matrix, those paths measure roughly `4.5x` to `20.3x` faster than the scalar implementations, depending on operation and span length.
- Third-party includes stay scoped to `packages/upr_runtime/src/decoder/runtime_byte_ops.cpp` and the internal declaration header, while the public UPR headers continue to expose the same API surface.
- Small-buffer thresholds are part of the backend dispatch policy so that short spans stay on the scalar path and large spans use the runtime-accelerated path.

## Where UPR Shines

- Static schemas with performance-sensitive decode paths. `upr_static_schema` keeps the compact UPR wire layout and delivers the best CPU results in every workload measured here.
- One schema model spanning dynamic and static integration. The same protocol definition can power discovery, tooling, reflective decode, resolved-id access, and generated fast paths without changing wire format.
- Compact binary messages with direct zero-copy access. In these workloads, UPR matches `packed_binary` on encoded size and materially undercuts `flatbuffers` on size.
- Large byte spans with checksum or text validation. The runtime backend materially reduces the byte-walking cost.

## Where UPR Is Less Performant Or Less Flexible

- `upr_reflective` is not the best choice for absolute hot-path performance on tiny or scalar-heavy messages. The generic introspection model is measurably slower than generated decode.
- `upr_resolved_ids` improves the generic path, but it still trails the generated path by a wide margin on short or field-dense messages.
- `upr_static_schema` is the strongest performance mode when the schema is known ahead of time. If the message layout falls outside the direct generated path, it falls back to the generic runtime and loses some of that advantage.
- `protobuf_lite` and `flatbuffers` still bring larger existing ecosystems, more off-the-shelf integrations, and more widely recognized interchange formats outside this repository.

## Selection Guide

- Choose `upr_static_schema` for product code with fixed schemas and performance-sensitive decode paths.
- Choose `upr_resolved_ids` when schemas are loaded at runtime but the hot path cannot afford repeated string lookup.
- Choose `upr_reflective` for tooling, discovery, workbench-style inspection, and dynamic integrations where runtime schema loading matters more than raw CPU time.
- Choose `protobuf_lite` or `flatbuffers` when ecosystem reach, cross-team standardization, or existing deployment constraints outweigh the benefits of UPR's compact wire format and unified runtime model.
- Treat `packed_binary` as a strong narrow baseline, not as a universal lower bound. In this suite, UPR's generated direct path beats it because the generated decoder validates and projects directly into borrowed typed values with fewer intermediate materializations.

## Interpretation Notes

- The performance ranking of `upr_static_schema` over the other measured implementations is robust in these workloads. The gaps are much larger than the observed run-to-run variation.
- The advantage of the generated path is biggest on short or scalar-heavy messages and smaller on large payloads.
- The runtime backend materially helps the byte-span primitives used inside the runtime.
- Encoded size matters. `flatbuffers` often looks stronger on throughput than on CPU time because it processes materially larger buffers.

## Benchmark Limits

- This is not an encode benchmark.
- This is not an end-to-end transport benchmark.
- This does not prove one format is best for every workload.
- This does not prove a generated UPR decoder will always beat a hand-written parser on every possible message layout.
- Differences below about `1%` should not be treated as strong evidence on this host.

## How To Reproduce

Full suite:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_benchmark.json \
  --benchmark_out_format=json
```

Focused decode runs:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_filter='decode_stream/.*' \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_decode.json \
  --benchmark_out_format=json
```

Focused byte-op runs:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_filter='byte_ops/.*' \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_byte_ops.json \
  --benchmark_out_format=json
```
