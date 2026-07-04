# Protocol Benchmark

This benchmark compares UPR stream decoding against three practical baselines:

- `packed_binary`: a hand-written parser over the same wire layout as UPR
- `protobuf_lite`: generated Protocol Buffers C++ with lite runtime
- `flatbuffers`: generated FlatBuffers C++ with verifier enabled

## Scope

This suite is narrow:

- It measures decode plus validation CPU cost over prebuilt corpora.
- It does not measure encoding, RPC stacks, transport latency, or disk I/O.
- It compares the same logical messages across formats.
- It keeps the same outer `uint16` little-endian frame prefix for all formats.
- It includes validation work instead of benchmarking unchecked parsing.

Validation included in the measured path:

- UPR: schema checks plus checksum validation
- `packed_binary`: bounds checks plus checksum validation
- `protobuf_lite`: message parsing and field extraction
- `flatbuffers`: buffer verification plus field extraction

## Workloads

- `blob_small`: `32` payload bytes, many messages, short-frame overhead dominates
- `blob_large`: `2048` payload bytes, fewer messages, payload traversal dominates
- `market_data`: fixed-width scalar-heavy message with symbol bytes and checksum

All corpora are generated from `16` deterministic seeds so a single payload pattern does not dominate the result.

## Methodology

- Harness: Google Benchmark
- Build: `bazel build -c opt //packages/upr_runtime:runtime_benchmark`
- Pinning used for measurements: `taskset -c 0`
- Timing mode: process CPU time
- Warmup per benchmark: `0.20 s`
- Minimum measured time per repetition: `0.75 s`
- Repetitions: `12`
- Runs captured for analysis: `2` full-suite runs plus `1` focused `market_data` rerun

Important host caveats from Google Benchmark on this machine:

- CPU scaling was enabled
- ASLR was enabled

That means very small gaps should be treated as noise. Median CPU time is the primary comparison metric. Throughput is still useful, but formats with larger encodings can look better on MiB/s simply because they process more bytes.

## Test Environment

- Date: `2026-04-04`
- Kernel: `Linux 6.17.0-19-generic x86_64 GNU/Linux`
- CPU: `13th Gen Intel(R) Core(TM) i9-13950HX`
- Logical CPUs: `32`

## Results

Primary table values below use full-suite run B medians. `Spread` is the median CPU-time difference between full-suite run A and run B.

### `blob_small`

| Protocol | Median CPU us | Spread | Median throughput MiB/s | Encoded B/msg | CPU CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| `upr` | 11145 | 0.01% | 213.10 | 36.00 | 0.44% |
| `packed_binary` | 2602 | 0.26% | 912.81 | 36.00 | 0.89% |
| `protobuf_lite` | 6637 | 0.68% | 381.34 | 38.49 | 0.90% |
| `flatbuffers` | 3553 | 1.58% | 1133.62 | 63.98 | 4.61% |

Read:

- `flatbuffers` was fastest among reusable schema-driven formats.
- `packed_binary` was still the raw-speed ceiling on the identical `36 B` wire shape.
- UPR trailed badly on short messages, which points to fixed per-message overhead rather than payload handling.
- `flatbuffers` paid for that speed with the largest encoding, about `1.78x` UPR's bytes per message.

### `blob_large`

| Protocol | Median CPU us | Spread | Median throughput MiB/s | Encoded B/msg | CPU CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| `upr` | 13833 | 0.07% | 580.04 | 2052.00 | 0.12% |
| `packed_binary` | 13941 | 0.04% | 575.52 | 2052.00 | 0.23% |
| `protobuf_lite` | 14461 | 0.18% | 555.78 | 2055.49 | 0.26% |
| `flatbuffers` | 13977 | 0.02% | 581.88 | 2079.98 | 0.16% |

Read:

- The four implementations were tightly clustered once payload bytes dominated the work.
- UPR and `packed_binary` were effectively tied on both size and CPU time.
- `flatbuffers` edged out the field on throughput, but only with a slightly larger encoding.
- The safe conclusion here is not "one protocol wins"; it is "large payloads hide most abstraction overhead."

### `market_data`

| Protocol | Median CPU us | Spread | Median throughput MiB/s | Encoded B/msg | CPU CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| `upr` | 28657 | 12.99% | 143.95 | 64.00 | 5.46% |
| `packed_binary` | 11359 | 0.43% | 363.16 | 64.00 | 0.86% |
| `protobuf_lite` | 17746 | 0.07% | 265.57 | 73.41 | 1.07% |
| `flatbuffers` | 12899 | 0.28% | 591.12 | 120.00 | 0.72% |

Focused sanity check:

- A dedicated `market_data` rerun landed at `28293 us` for UPR, `11129 us` for `packed_binary`, `17357 us` for `protobuf_lite`, and `12887 us` for `flatbuffers`.
- That focused rerun stayed within `0.10%` to `2.22%` of full-suite run B for all four protocols, so full-suite run B is treated as the representative result.
- The slower full-suite run A UPR result was therefore treated as an outlier, not as the center of the conclusion.

Read:

- On scalar-heavy fixed layouts, UPR currently leaves substantial performance on the table.
- `packed_binary` remained the best raw baseline at the same encoded size.
- `flatbuffers` was fastest among reusable schema-driven formats, but its message size was almost `1.88x` UPR's.
- `protobuf_lite` sat between UPR and `flatbuffers` on both speed and size.

## Safe Conclusions

- UPR is currently not competitive on short, fixed-width messages where per-message overhead dominates.
- UPR is competitive on large payloads, where message-body traversal dominates total cost.
- `packed_binary` remains the right upper bound for this exact wire layout, not the right product default for schema evolution or tooling.
- `flatbuffers` won the measured reusable-format speed contest in `blob_small` and `market_data`, but it also produced the largest encodings.
- `protobuf_lite` was the most middle-of-the-road result: smaller than `flatbuffers`, faster than UPR in two scenarios, and slightly slower than the leaders everywhere.

## What Conclusions Are Not Safe

- This is not an encode benchmark.
- This is not an end-to-end transport benchmark.
- This does not prove one protocol is "best" for every workload.
- Throughput alone is not enough when encoded sizes differ materially.
- Tiny deltas under about `1%` on this host are not strong evidence because CPU scaling and ASLR were still enabled.

## Criteria Needed Before Choosing a Format

- Decode CPU time on the real message shape
- Encoded size on the real transport budget
- Validation guarantees actually required by the product
- Whether zero-copy access is a real fit or just a benchmark advantage
- Schema evolution, code generation, and tooling cost
- Memory allocation behavior in the hot path
- Integration model: dynamic runtime schema vs generated static bindings

If those criteria disagree, the correct conclusion is usually "pick the format that matches the product constraints," not "pick the fastest bar in one chart."

## How To Reproduce

Full suite:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_benchmark.json \
  --benchmark_out_format=json
```

Focused rerun for the most noise-sensitive scenario:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_filter='decode_stream/.*/market_data' \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_benchmark_market.json \
  --benchmark_out_format=json
```
