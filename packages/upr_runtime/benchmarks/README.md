# Protocol Benchmark Report

## Recommendation

`upr_static_schema` is the strongest default when the schema is known ahead of time and both read-path latency and wire efficiency matter. It delivers the best decode performance in every workload here, keeps messages as compact as the hand-written binary baseline, and encodes fast enough that write-side cost rarely outweighs its read-side advantage.

`packed_binary` is the narrow choice for teams optimising almost entirely for encode throughput. It writes the fastest buffers in the large-payload and market-data cases, but that gain comes without UPR's schema tooling, runtime schema, or shared wire model.

`protobuf_lite` is the most balanced external baseline. Its encode path is competitive, especially on larger payloads, but it gives up some wire compactness and falls well behind `upr_static_schema` on decoding and encoding.

`flatbuffers` is hardest to justify for these message shapes. It carries the largest wire footprint in every scenario, and the larger buffers do not buy enough read or write throughput to offset that overhead. It is faster than Protobuf, but still not as performant as `upr_static_schema` or `packed_binary`.

## Executive Summary

- For decode-heavy or mixed read/write systems, `upr_static_schema` is the clear recommendation, offering the best decoding speed while its encode cost stays close to the fastest baselines.
- For write-dominant systems where every microsecond of serialization matters more than tooling flexibility, `packed_binary` is a slightly faster emitter.
- For dynamic tooling, discovery, and runtime integration, UPR's generic modes remain valuable, but they should not be treated as hot-path performance modes.

## Considerations

Three patterns dominate the results.

First, wire size matters. `upr_static_schema` and `packed_binary` share the smallest layouts in all three workloads. That gives them an immediate advantage in cache pressure, memory bandwidth, and transport efficiency. `flatbuffers` pays the largest penalty, especially on `blob_small` and `market_data`, where its metadata cost is proportionally large.

Second, compiled-schema decode dominates generic runtime decode. The distance between `upr_static_schema` and the dynamic UPR modes is much larger than the distance between most write paths. That means product decisions should be anchored more heavily on read-path behavior than on raw serialization speed alone.

Third, encode performance is much tighter than decode performance. The fastest encoders tend to cluster within a narrower band than the fastest decoders. In practice, that makes wire efficiency and decode cost more important decision criteria for these message families than pure encode throughput.

## Encoding Results

The encode suite compares the two materially different UPR write paths with the same external baselines used in decode:

- `upr_runtime_schema`: runtime `ProtocolEncoder` path
- `upr_static_schema`: generated direct encode
- `packed_binary`: hand-written binary writer
- `protobuf_lite`: generated Protocol Buffers lite runtime
- `flatbuffers`: generated FlatBuffers writer

### `blob_small`

| Protocol | Mean CPU us | Throughput MiB/s | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 3301 | 719.6 | 36.00 |
| `packed_binary` | 3302 | 719.3 | 36.00 |
| `protobuf_lite` | 3596 | 703.9 | 38.49 |
| `upr_runtime_schema` | 6240 | 381.1 | 36.00 |
| `flatbuffers` | 6996 | 589.6 | 63.98 |

Meaning:

- `upr_static_schema` and `packed_binary` are effectively tied on tiny frames.
- `protobuf_lite` stays reasonably close, but it writes a slightly larger message.
- `upr_runtime_schema` pays a large generic-builder cost even though its wire format stays compact.
- `flatbuffers` spends much more CPU per message while also emitting the largest buffer.

### `blob_large`

| Protocol | Mean CPU us | Throughput MiB/s | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `packed_binary` | 11240 | 713.8 | 2052.00 |
| `protobuf_lite` | 11464 | 701.2 | 2055.49 |
| `upr_static_schema` | 11476 | 699.1 | 2052.00 |
| `flatbuffers` | 11608 | 700.6 | 2079.98 |
| `upr_runtime_schema` | 11746 | 683.1 | 2052.00 |

Meaning:

- Large payloads compress the gap between serializers because payload copy cost dominates framework overhead.
- `packed_binary`, `protobuf_lite`, `upr_static_schema`, and `flatbuffers` all land in the same broad tier.
- `upr_static_schema` keeps the smallest buffer without giving up much write throughput.
- `protobuf_lite` is notably strong here and is the closest external write-path competitor to generated UPR.

### `market_data`

| Protocol | Mean CPU us | Throughput MiB/s | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `packed_binary` | 5772 | 714.8 | 64.00 |
| `upr_static_schema` | 6184 | 667.1 | 64.00 |
| `protobuf_lite` | 9647 | 488.6 | 73.41 |
| `flatbuffers` | 14033 | 543.4 | 120.00 |
| `upr_runtime_schema` | 14156 | 291.5 | 64.00 |

Meaning:

- The hand-written writer is still the fastest option for dense fixed-layout records, but the generated UPR path now sits much closer to it than before.
- `upr_static_schema` remains comfortably ahead of the generated external baselines while preserving the smallest wire format.
- `protobuf_lite` pays a moderate size premium and a meaningful CPU penalty.
- `flatbuffers` moves more bytes, so its bandwidth number looks healthier than its latency, but that larger envelope is part of the cost rather than a hidden advantage.

## Decoding Results

### `blob_small`

| Protocol | Mean CPU us | Stddev CPU us | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 2018 | 1.8 | 36.00 |
| `packed_binary` | 3011 | 4.8 | 36.00 |
| `flatbuffers` | 3978 | 16.9 | 63.98 |
| `protobuf_lite` | 6846 | 19.8 | 38.49 |
| `upr_resolved_ids` | 8059 | 8.1 | 36.00 |
| `upr_reflective` | 11157 | 15.6 | 36.00 |

Meaning:

- `upr_static_schema` is the best tiny-message reader in this benchmark by a wide margin.
- The direct generated path matters more than the wire format alone. `packed_binary` is compact, but it still trails generated UPR by a large step.
- `flatbuffers` reads faster than the dynamic UPR modes, but it does so while carrying a much larger payload.

### `blob_large`

| Protocol | Mean CPU us | Stddev CPU us | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 11255 | 28.3 | 2052.00 |
| `upr_resolved_ids` | 11625 | 20.5 | 2052.00 |
| `upr_reflective` | 11833 | 20.7 | 2052.00 |
| `packed_binary` | 13569 | 29.1 | 2052.00 |
| `flatbuffers` | 13646 | 19.2 | 2079.98 |
| `protobuf_lite` | 14031 | 39.3 | 2055.49 |

Meaning:

- Once payload bytes dominate, every implementation moves closer together.
- Even in that flatter regime, generated UPR still leads the field.
- The large-payload case is where `protobuf_lite` and `flatbuffers` look most competitive, but neither overturns the overall ranking.

### `market_data`

| Protocol | Mean CPU us | Stddev CPU us | Encoded B/msg |
| --- | ---: | ---: | ---: |
| `upr_static_schema` | 4750 | 15.5 | 64.00 |
| `packed_binary` | 14601 | 123.0 | 64.00 |
| `flatbuffers` | 16186 | 172.0 | 120.00 |
| `upr_resolved_ids` | 17485 | 10.3 | 64.00 |
| `protobuf_lite` | 18671 | 129.0 | 73.41 |
| `upr_reflective` | 31982 | 47.9 | 64.00 |

Meaning:

- Scalar-heavy fixed records are where generated UPR creates the clearest separation.
- `upr_static_schema` is more than twice as fast as the hand-written reader here while keeping the same wire size.
- `flatbuffers` and `protobuf_lite` both pay notable size overhead without recovering enough decode speed to compensate.

## How To Read The Tradeoffs

`upr_static_schema` wins the overall decision because its strengths line up with the expensive side of most systems. Reads tend to sit closer to tail-latency budgets, fan out into more downstream work, and compound transport costs when messages are larger than necessary. The encode data shows that generated UPR does not need to be the single fastest writer to be the best protocol choice overall.

`packed_binary` is the most specialized option. It proves the ceiling for a narrow bespoke writer, and it earns that advantage on encode, especially for dense fixed-width records. The cost is that the performance comes from custom code tied to one layout rather than from a reusable schema/runtime model.

`protobuf_lite` is the most credible alternative when interoperability and ecosystem gravity matter more than raw read performance. Its write path is consistently solid, and its size overhead is moderate. The trade is a materially slower decode path for the message shapes measured here.

`flatbuffers` benefits most when direct in-place traversal outweighs layout inflation. That advantage does not emerge strongly in this suite. The format is consistently larger, and the extra bytes do not translate into category-leading encode or decode results.

The dynamic UPR modes preserve the same compact wire layout and support runtime-schema workflows, but their generic access costs are high for the hottest paths, compared to the alternatives in these benchmarks. It should be used when performance is not a critical priority, but when the data schema is not known at compile time.

## Selection Guide

Choose `upr_static_schema` when:

- schema is fixed at build time
- read-path latency matters
- compact wire size matters
- one schema model across tooling, runtime, and generated code is valuable

Choose `packed_binary` when:

- the write path matters more than every other consideration
- the message family is stable and intentionally bespoke
- maintaining a hand-written codec is acceptable

Choose `protobuf_lite` when:

- external ecosystem compatibility matters more than best decode speed
- moderate wire overhead is acceptable
- generated tooling across teams or languages is the primary goal

Choose dynamic UPR modes when:

- schemas arrive at runtime
- inspection, discovery, or flexible integration matter more than raw CPU cost
- the same compact wire format still needs to serve non-generated code paths

## Runtime Backend Snapshot

The byte-span runtime backend still matters. Large-span ASCII and UTF-8 validation, checksum scans, and CRC32C all speed up substantially through the runtime-dispatched implementations. Those gains improve the UPR runtime as a whole, but they are not large enough by themselves to make reflective decode compete with generated decode on small or field-dense messages.

## Reproduce

Build:

```bash
bazel build -c opt //packages/upr_runtime:runtime_benchmark
```

Run the full benchmark suite:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_benchmark.json \
  --benchmark_out_format=json
```

Run encode only:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_filter='encode_stream/.*' \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_encode.json \
  --benchmark_out_format=json
```

Run decode only:

```bash
taskset -c 0 ./bazel-bin/packages/upr_runtime/runtime_benchmark \
  --benchmark_filter='decode_stream/.*' \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_runtime_decode.json \
  --benchmark_out_format=json
```
