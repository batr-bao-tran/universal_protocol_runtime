"""Python equivalent of ``examples/cpp/src/sensor_packet_encode_example.cpp``.

Encodes a ``SensorPacket`` from ``examples/schema/hardware_telemetry.upr`` and
round-trips it back through decode. This schema exercises the trickier encoder
features:

* a reserved, alignment-padded gap (``pad: reserved[2] align(4)``) that the
  encoder fills automatically;
* a length-prefixed byte blob (``sample_bytes: bytes[sample_bytes_len]``) whose
  length field is derived from the data you supply - you never set
  ``sample_bytes_len`` by hand;
* a ``validate(...)`` rule documenting the producer invariant
  ``sample_bytes_len == sample_count * 4`` when ``version == 2``.

The C++ example also compares the *segmented / zero-copy* builder against the
contiguous builder. Zero-copy payload attachment is a C++-only encoder
optimisation; the Python/TypeScript runtimes always produce a single contiguous
frame, so this example focuses on the contiguous path (which is byte-identical
to the C++ contiguous output).
"""

from __future__ import annotations

from generated import hardware_telemetry
from generated.hardware_telemetry import CODEC, SensorPacket

SAMPLE_BYTES = bytes([0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23])


def main() -> int:
    # Note we omit ``message_type`` (expected constant), ``pad`` (reserved) and
    # ``sample_bytes_len`` (derived from sample_bytes) - the codec supplies them.
    packet = {"version": 2, "sample_count": 2, "sample_bytes": SAMPLE_BYTES}
    encoded = CODEC.encode("SensorPacket", packet)

    decoded = CODEC.decode("SensorPacket", encoded)
    assert decoded["sample_bytes"] == SAMPLE_BYTES
    assert decoded["sample_bytes_len"] == len(SAMPLE_BYTES)

    print(f"sensor_packet_encoded bytes={len(encoded)} data={' '.join(str(b) for b in encoded)}")
    print(
        f"decoded_sample_count={decoded['sample_count']} "
        f"decoded_sample_bytes_len={decoded['sample_bytes_len']} "
        f"reserved_pad={decoded['pad'].hex()}"
    )

    # The validate() rule is a documented producer invariant; mirror the check so
    # malformed packets are caught before they hit the wire.
    if decoded["version"] == 2 and decoded["sample_bytes_len"] != decoded["sample_count"] * 4:
        raise ValueError("validate(sample_bytes_len == sample_count * 4) violated")

    # Typed encode/decode round-trip via the generated dataclass.
    typed = SensorPacket(version=2, sample_count=2, sample_bytes=SAMPLE_BYTES)
    typed_frame = typed.encode()
    print(f"typed_match={'yes' if typed_frame == encoded else 'no'}")
    print(f"sensor_packet_hex={encoded.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
