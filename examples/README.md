# Examples

These examples show how to use the runtime from C++, Python, and TypeScript.
The protocol examples all use the shared schemas in [`schema/`](schema), so the
same message bytes can be exchanged across languages.

```
examples/
├── schema/        # shared .upr / .yaml schemas (used by all languages)
├── cpp/           # C++ examples (include/ + src/)
├── python/        # Python examples
└── typescript/    # TypeScript examples
```

## Shared schemas

- `schema/market_data.upr` / `.yaml` — the compact order-entry demo (imports `order_types`).
- `schema/advanced_market_data.upr` / `.yaml` — repeating groups, tagged variants, presence-gated optionals.
- `schema/hardware_telemetry.upr` / `.yaml` — reserved gaps, alignment hints, dynamic byte blobs, and a validation rule.

## C++

Full C++ runtime examples. See [`cpp/README.md`](cpp/README.md).

- `cpp/src/upr_demo.cpp` loads the market-data schema, decodes a fixed-size framed stream, runs discovery, and writes `upr_demo_workbench.html`.
- `cpp/src/market_data_decode_example.cpp` decodes snapshot, quote, and trade messages, demonstrates partial decode, and writes a workbench.
- `cpp/src/sensor_packet_encode_example.cpp` compares segmented (zero-copy) and contiguous encode for `SensorPacket` and round-trips through decode.
- `cpp/src/hardware_byte_stream_example.cpp` feeds length-prefixed packets through transport, framing, and stream-runtime decode.
- `cpp/src/network_transport_example.cpp` sends an encoded `SensorPacket` through Unix sockets and TCP loopback using `FrameChannel` and `UprSession`.

Run them with:

```bash
bazel run //examples/cpp:upr_demo
bazel run //examples/cpp:market_data_decode_example
bazel run //examples/cpp:sensor_packet_encode_example
bazel run //examples/cpp:hardware_byte_stream_example
bazel run //examples/cpp:network_transport_example
```

## Generated bindings

The Python and TypeScript examples import protocol modules generated from the
shared schemas. Bazel generates them as build outputs (under `bazel-bin/`, not
committed) whenever you build or run an example — no manual step required.

## Python

Native-backed generated codecs plus pure-Python framing/session helpers, run
entirely by Bazel. See [`python/README.md`](python/README.md).

```bash
bazel run //examples/python:market_data_demo
```

## TypeScript

Dependency-free `Codec` plus framing/session helpers. See [`typescript/README.md`](typescript/README.md).
Built and run entirely by Bazel (no `npm install`):

```bash
bazel run //examples/typescript:market_data_demo
```

## Feature coverage across languages

| Feature | C++ | Python | TypeScript |
| --- | :---: | :---: | :---: |
| Encode / decode by message name | ✅ | ✅ | ✅ |
| Repeating groups, variants, presence | ✅ | ✅ | ✅ |
| Derived length/count, reserved, alignment | ✅ | ✅ | ✅ |
| Length-prefixed framing + streaming decode | ✅ | ✅ | ✅ |
| `UPR1` session handshake + checksums | ✅ | ✅ | ✅ |
| TCP / Unix socket adapters (`upr_adapters`) | ✅ | — | — |
| Partial decode (`DecodeFieldMask`) | ✅ | — | — |
| Segmented / zero-copy encode | ✅ | — | — |
| Transport + `StreamRuntime`, discovery, workbench | ✅ | — | — |

Features marked “—” are intentionally outside the Python/TypeScript runtime
surface. In Python and TypeScript, bring your own socket/pipe I/O and feed bytes
through the framing helpers.
