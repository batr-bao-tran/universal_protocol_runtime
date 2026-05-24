# Examples

- `schema/market_data.upr` and `schema/market_data.yaml` are the original compact order-entry demo schemas used by `upr_demo`.
- `src/upr_demo.cpp` loads the original market-data schema, decodes a fixed-size framed stream, runs discovery on the sample frames, and writes `upr_demo_workbench.html`.
- `schema/advanced_market_data.upr` and `schema/advanced_market_data.yaml` show repeating groups, tagged variants, and presence-gated optional fields.
- `src/market_data_decode_example.cpp` decodes snapshot, quote, and trade-style market-data messages, demonstrates partial decode, and writes `advanced_market_data_workbench.html`.
- `schema/hardware_telemetry.upr` and `schema/hardware_telemetry.yaml` show reserved gaps, alignment hints, and validation rules for a hardware-facing packet.
- `src/sensor_packet_encode_example.cpp` compares segmented and contiguous encode for `SensorPacket`, round-trips through decode, and writes `sensor_packet_encode_workbench.html`.
- `src/hardware_byte_stream_example.cpp` feeds length-prefixed hardware packets through transport, framing, and stream runtime decode, then writes `hardware_telemetry_workbench.html`.

Run them with:

```bash
bazel run //examples:upr_demo
bazel run //examples:market_data_decode_example
bazel run //examples:sensor_packet_encode_example
bazel run //examples:hardware_byte_stream_example
```
