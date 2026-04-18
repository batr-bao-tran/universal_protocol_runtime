# UPR Schema Guide

UPR schemas describe binary protocol layouts. The recommended file extension is `.upr`.

## File Structure

A schema usually contains:

- one `protocol` declaration
- optional `import` declarations
- optional `enum` declarations
- optional `struct` declarations
- one or more `message` declarations

```text
protocol market_data

import "examples/order_types.upr"

enum Side: uint8 { 1 = Buy, 2 = Sell }

struct Header {
  flags: uint16_be {
    version @ 13:3
    urgent @ 12:1
  }
}

message Order {
  message_type: uint8 = 1
  header: Header
  symbol: ascii[4]
  side: Side
}
```

Imported files can be enum or struct libraries and do not need their own `protocol` or `message` declarations. Only the root schema needs a `protocol` name and at least 1 `message`.

## Imports

Use `import` to reuse shared enums, structs, or message fragments:

```text
protocol market_data

import "examples/order_types.upr"

message Order {
  message_type: uint8 = 1
  side: Side
  order_type: OrderType
}
```

Import resolution is done once when loading from a file. You can use the loaded schema directly at runtime, or compile it first for the lowest-overhead decode path. The compiler receives a flattened schema, so compiled and generated decoding do not pay any import-resolution cost.

Import path rules:

- `./shared/types.upr` and `../shared/types.upr` are resolved relative to the importing file
- `examples/order_types.upr` is resolved from the Bazel workspace root when one is present

YAML uses the equivalent top-level `imports` list:

```yaml
protocol: market_data
imports:
  - examples/order_types.yaml
```

## Supported Field Types

- Unsigned integers: `uint8`, `uint16`, `uint32`, `uint64`
- Signed integers: `int8`, `int16`, `int32`, `int64`
- Floating point: `float32`, `float64`
- Big-endian scalars: append `_be`, for example `uint16_be`
- Raw bytes: `bytes[16]` or `bytes[length_field]`
- Strings: `ascii[8]`, `utf8[32]`, `string[8]`
- Named structs: `header: Header`
- Named enums: `side: Side`
- Inline enums: `side: enum<uint8> { 1 = Buy, 2 = Sell }`

## Enums

Enums can be declared and used by name:

```text
enum Side: uint8 { 1 = Buy, 2 = Sell }

message Order {
  side: Side
}
```

Inline enums are also supported:

```text
side: enum<uint8> { 1 = Buy, 2 = Sell }
```

## Bitfields

Bitfields are defined inside an unsigned, signed, or enum scalar field:

```text
flags: uint16 {
  version @ 13:3
  urgent @ 12:1
}
```

Bitfield form is:

```text
name @ offset_bits:width_bits
```

Signed bitfields are supported:

```text
delta @ 4:4 signed
```

Bitfields can also carry enum labels:

```text
kind @ 0:2 { 1 = Quote, 2 = Trade }
```

## Fixed Values

Use `=` to require a scalar field to match a specific value:

```text
message_type: uint8 = 1
```

## Variable-Length Fields

Use another earlier field as the size source:

```text
payload_length: uint16
payload: bytes[payload_length]
symbol: ascii[name_length]
```

## Checksums

Checksums are for integrity checking. A checksum field stores a value computed from part of the frame, so the decoder can verify that the bytes were not corrupted or truncated.

```text
checksum: uint8 checksum(xor8)
checksum: uint16 checksum(sum16)
```

If you omit the range, it defaults to everything from `frame_start` up to just before the checksum field, so these two forms are equivalent:

```text
checksum: uint8 checksum(xor8)
checksum: uint8 checksum(xor8, frame_start, before_self)
```

The anchors define the byte range to hash:

- `frame_start` means the beginning of the frame.
- `frame_end` means the end of the frame.
- `before_self` means the byte immediately before the checksum field.
- `after_self` means the byte immediately after the checksum field.
- `<field>.start` means the start of another field in the same layout.
- `<field>.end` means the end of another field in the same layout.

Examples:

```text
checksum: uint16 checksum(crc16_ccitt, frame_start, before_self)
checksum: uint16 checksum(crc32, header.start, payload.end)
checksum: uint8 checksum(xor8, after_self, frame_end)
```

Algorithms:

- `xor8`
- `sum16`
- `crc16_ccitt`
- `crc32`
- `crc32c`

## Messages

Messages contain ordered fields. Field order defines wire order.

```text
message Trade {
  message_type: uint8 = 3
  instrument_id: uint32
  price: float64
  quantity: uint32
}
```

## YAML Support

YAML schemas are also supported for interoperability, but `.upr` is the most concise format for authoring. Both formats support runtime loading and compiled use.

## Bazel

When a schema imports other schema files, make those files part of the target's runfiles. A simple pattern is to wrap the root schema and its imported files in a `filegroup` or `upr_schema_library` target and add that target to `data`.
