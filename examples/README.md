# Examples

The same protocols are demonstrated in **C++, Python, and TypeScript**, all
driven by the shared schemas in [`schema/`](schema). Equivalent examples across
languages produce byte-for-byte identical frames.

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

- `cpp/src/upr_demo.cpp` loads the market-data schema, decodes a fixed-size framed stream, runs discovery, and writes `upr_demo_workbench.html`.
- `cpp/src/market_data_decode_example.cpp` decodes snapshot, quote, and trade messages, demonstrates partial decode, and writes a workbench.
- `cpp/src/sensor_packet_encode_example.cpp` compares segmented (zero-copy) and contiguous encode for `SensorPacket` and round-trips through decode.
- `cpp/src/hardware_byte_stream_example.cpp` feeds length-prefixed packets through transport, framing, and stream-runtime decode.

Run them with:

```bash
bazel run //examples:upr_demo
bazel run //examples:market_data_decode_example
bazel run //examples:sensor_packet_encode_example
bazel run //examples:hardware_byte_stream_example
```

## Generated bindings

The Python and TypeScript examples import protocol modules generated from the
shared schemas. These `generated/` folders are **not** committed — generate them
once after checkout (and again whenever a schema changes):

```bash
python3 examples/generate_bindings.py
```

## Python

Pure-Python `Codec` plus framing/session helpers. See [`python/README.md`](python/README.md).

```bash
pip install -e packages/upr_python
cd examples/python
python3 market_data_demo.py
```

## TypeScript

Dependency-free `Codec` plus framing/session helpers. See [`typescript/README.md`](typescript/README.md).

```bash
cd examples/typescript
npm install && npm run build:runtime
npm run market-data-demo
```

## Feature coverage across languages

| Feature | C++ | Python | TypeScript |
| --- | :---: | :---: | :---: |
| Encode / decode by message name | ✅ | ✅ | ✅ |
| Repeating groups, variants, presence | ✅ | ✅ | ✅ |
| Derived length/count, reserved, alignment | ✅ | ✅ | ✅ |
| Length-prefixed framing + streaming decode | ✅ | ✅ | ✅ |
| `UPR1` session handshake + checksums | ✅ | ✅ | ✅ |
| Partial decode (`DecodeFieldMask`) | ✅ | — | — |
| Segmented / zero-copy encode | ✅ | — | — |
| Transport + `StreamRuntime`, discovery, workbench | ✅ | — | — |

Features marked “—” are intentionally outside the Python/TypeScript runtime
surface; the language examples document the recommended alternative inline.
