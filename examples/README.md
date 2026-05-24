# Examples

The examples are now split into two separate packages because the domains are unrelated:

- `advanced_market_data.upr` and `advanced_market_data.yaml` cover repeating groups, tagged variants, and presence-gated optional fields for market data
- `hardware_telemetry.upr` and `hardware_telemetry.yaml` cover reserved gaps, alignment hints, and validation rules for hardware-facing sample payloads

Layout:

- `examples/schema/` contains `.upr` and `.yaml` schema files
- `examples/include/` contains shared example support headers
- `examples/src/` contains runnable example binaries

The runtime examples are split by job:

- `market_data_decode_example.cpp` decodes multiple market-data message shapes, shows presence/variant behavior, emits a partial-decode example, and writes a workbench HTML file
- `sensor_packet_encode_example.cpp` compares segmented and contiguous hardware-packet encode paths, round-trips the result through decode, and writes a workbench HTML file
- `hardware_byte_stream_example.cpp` reads a length-prefixed hardware byte stream through transport, framing, and runtime decode, then writes a workbench HTML file from the captured frames

Run them with:

```bash
bazel run //examples:market_data_decode_example
bazel run //examples:sensor_packet_encode_example
bazel run //examples:hardware_byte_stream_example
```
