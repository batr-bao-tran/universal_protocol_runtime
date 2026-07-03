# Python examples

These examples use the Python runtime package with protocol modules generated
from the shared schemas in [`../schema`](../schema). The generated modules expose
both a dict-oriented `CODEC` and typed dataclasses for each message.

The Python runtime ships a dependency-free `Codec` plus framing/session helpers
(see [`packages/upr_python`](../../packages/upr_python)). It does not provide
socket transports; use normal Python socket/file APIs and pass received bytes to
`FrameDecoder`.

## Layout

| File | Features shown |
| --- | --- |
| `market_data_demo.py` | encode + fixed-size frame decode, enums, `ascii[N]`, `float32`, typed dataclasses |
| `market_data_decode_example.py` | repeating groups, tagged variants, presence-gated optionals |
| `sensor_packet_encode_example.py` | derived length fields, reserved + alignment gaps, dynamic `bytes`, validation invariant |
| `hardware_byte_stream_example.py` | length-prefixed framing + streaming `FrameDecoder` |
| `framing_and_session_example.py` | `encode_frame`/`try_read_frame`/`iter_frames`/`FrameDecoder`, `UPR1` handshake, checksums |

## Run

Each example is a Bazel `py_binary`. Bazel generates the protocol module, builds
its native codec, and links the runtime automatically:

```bash
bazel run //examples/python:market_data_demo
bazel run //examples/python:market_data_decode_example
bazel run //examples/python:sensor_packet_encode_example
bazel run //examples/python:hardware_byte_stream_example
bazel run //examples/python:framing_and_session_example
```

## Generated modules

Each `generated.<schema>` module is built by the `upr_python_bindings` macro
(see [`//tools:upr_python_defs.bzl`](../../tools/upr_python_defs.bzl)), which
runs `upr-gen` and compiles the generated pybind11 native codec. The module's
`CODEC` encodes and decodes every layout through that native codec, producing
output identical to the pure-Python `Codec`.

To generate a module by hand (facade only):

```bash
bazel run //packages/upr_codegen:upr-gen -- \
    --lang python \
    --input examples/schema/advanced_market_data.upr \
    --output advanced_market_data.py
```

Add `--native-output`/`--native-header-output` to also emit the native codec,
then build it as an extension next to the facade; the facade imports it and uses
it for every layout.
