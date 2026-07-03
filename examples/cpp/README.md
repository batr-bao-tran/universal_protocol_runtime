# C++ examples

These examples exercise the full C++ runtime: schema loading, compilation,
encoding, decoding, stream polling, transport adapters, discovery, and HTML
workbench output.

| File | Features shown |
| --- | --- |
| `src/upr_demo.cpp` | runtime schema load/compile, fixed-size framing, stream polling, discovery, workbench output |
| `src/market_data_decode_example.cpp` | repeating groups, tagged variants, presence-gated fields, partial decode, workbench output |
| `src/sensor_packet_encode_example.cpp` | contiguous and segmented encode, zero-copy payload attachment, round-trip decode |
| `src/hardware_byte_stream_example.cpp` | length-prefixed framing over chunked byte-stream input |
| `src/network_transport_example.cpp` | Unix socket pair and TCP loopback transports, `FrameChannel`, `UprSession`, framed payload exchange |

Run all C++ examples from the repository root:

```bash
bazel run //examples/cpp:upr_demo
bazel run //examples/cpp:market_data_decode_example
bazel run //examples/cpp:sensor_packet_encode_example
bazel run //examples/cpp:hardware_byte_stream_example
bazel run //examples/cpp:network_transport_example
```
