# universal-protocol-runtime (Python)

A **dependency-free, pure-Python** encoder/decoder for
[Universal Protocol Runtime](../../README.md) (UPR) schemas, plus framing and
session helpers that interoperate byte-for-byte with the C++ runtime.

* No native extension, no third-party dependencies — just the standard library.
* Byte-compatible with the C++ direct and dynamic codecs (scalars, signed,
  floats, bytes, strings, structs, collections, tagged variants,
  presence/condition-gated optionals, reserved fills, expected constants and
  the built-in checksums `xor8`, `sum16`, `crc16_ccitt`, `crc32`, `crc32c`).
* Rich `DecodeError` with the failing field path and byte offset.
* Length-prefixed framing and the `UPR1` session handshake.

## Install

```bash
pip install universal-protocol-runtime
```

For local development from this repository:

```bash
pip install -e packages/upr_python
```

## Generate a protocol module

```bash
upr-gen --lang python --input my_protocol.upr --output my_protocol.py
# or, before the CLI is on PATH:
bazel run //packages/upr_codegen:generate_bindings -- \
    --lang python --input my_protocol.upr --output my_protocol.py
```

## Use it

```python
from universal_protocol_runtime import encode_frame, FrameDecoder
from my_protocol import CODEC

# Encode a message to a frame (dict-based API).
payload = CODEC.encode("Quote", {"price": 10125, "size": 5})

# Decode a frame back to a dict (raises DecodeError with field + offset on bad data).
values = CODEC.decode("Quote", payload)

# Decode a packed sequence of struct records without manual bookkeeping.
records = CODEC.decode_sequence("InstrumentId", blob)

# Typed dataclasses are generated too.
from my_protocol import Quote
quote = Quote.decode(payload)
raw = quote.encode()

# Wire framing for stream transports (interoperates with the C++ FrameChannel).
wire = encode_frame(payload)              # 4-byte little-endian length prefix
decoder = FrameDecoder()
for frame in decoder.feed(wire):
    CODEC.decode("Quote", frame)
```

See [`docs/WIRE_SPEC.md`](../../docs/WIRE_SPEC.md) for the normative frame and
session byte layout.
