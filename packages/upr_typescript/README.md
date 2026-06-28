# universal-protocol-runtime (TypeScript / JavaScript)

A **dependency-free** encoder/decoder for
[Universal Protocol Runtime](../../README.md) (UPR) schemas, plus framing and
session helpers that interoperate byte-for-byte with the C++ and Python
runtimes.

* Zero runtime dependencies — uses only built-in `Uint8Array`, `DataView`,
  `TextEncoder`/`TextDecoder` and `BigInt`.
* Byte-compatible with the C++ and Python runtimes (scalars, signed, floats,
  bytes, strings, structs, collections, tagged variants, presence/condition-gated
  optionals, reserved fills, expected constants and the built-in checksums
  `xor8`, `sum16`, `crc16_ccitt`, `crc32`, `crc32c`).
* Rich `DecodeError` carrying the failing field path and byte offset.
* Length-prefixed framing and the `UPR1` session handshake.
* Ships as ESM with `.d.ts` typings. Sources use standard NodeNext `.js`
  import specifiers so emitted JavaScript and declarations resolve identically.

## Install

```bash
npm install universal-protocol-runtime
```

## Generate a protocol module

```bash
# Using the standalone CLI:
upr-gen --lang typescript --input my_protocol.upr --output my_protocol.ts

# or, from this repository before the CLI is on PATH:
bazel run //packages/upr_codegen:generate_bindings -- \
    --lang typescript --input my_protocol.upr --output my_protocol.ts
```

The generated module imports the runtime from `universal-protocol-runtime` by
default; pass `--runtime-import <specifier>` to point at a different location
(for example a relative path during local development).

## Use it

```ts
import { encodeFrame, FrameDecoder } from "universal-protocol-runtime";
import { Quote } from "./my_protocol.ts";

// Encode a typed value to a frame.
const payload = Quote.encode({ price: 10125, size: 5 });

// Decode a frame back to a typed value (throws DecodeError with field + offset
// on bad data).
const quote = Quote.decode(payload);

// Decode a packed sequence of records without manual bookkeeping.
const records = Quote.decodeSequence(blob);

// Wire framing for stream transports (interoperates with the C++ FrameChannel).
const wire = encodeFrame(payload);          // 4-byte little-endian length prefix
const decoder = new FrameDecoder();
for (const frame of decoder.feed(wire)) {
  Quote.decode(frame);
}
```

Wide integers (7- and 8-byte fields) decode to `bigint`; narrower integers and
floats decode to `number`. On encode, pass `bigint` for wide values unless the
value is known to be within JavaScript's safe integer range.

## Build / test

```bash
npm install
npm run typecheck      # tsc --noEmit
npm test               # builds dist/ then runs node --experimental-strip-types tests
npm run build          # emit dist/ (.js + .d.ts)
```

See [`docs/WIRE_SPEC.md`](../../docs/WIRE_SPEC.md) for the normative frame and
session byte layout.
