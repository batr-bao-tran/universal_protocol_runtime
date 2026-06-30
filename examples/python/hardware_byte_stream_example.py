"""Python equivalent of ``examples/cpp/src/hardware_byte_stream_example.cpp``.

The C++ example pushes two length-prefixed ``SensorPacket`` frames through a
``SpanTransport`` (which hands out small chunks) into a ``LengthPrefixedFramer``
(2-byte little-endian prefix) wired to a ``StreamRuntime`` that decodes each
reassembled frame.

Python has no ``StreamRuntime``; instead the ``framing`` helpers provide a
``FrameDecoder`` that accumulates bytes from a transport and yields complete
payloads. Using ``prefix_width=2`` makes the wire bytes identical to the C++
``LengthPrefixedFramer`` configuration, so a Python peer interoperates with a
C++ peer.
"""

from __future__ import annotations

from universal_protocol_runtime import FrameDecoder, encode_frame

from generated.hardware_telemetry import CODEC

# 2-byte little-endian length prefix, matching the C++ LengthPrefixedFramer.
PREFIX_WIDTH = 2
# Simulated transport read size, matching the C++ SpanTransport(..., 5).
TRANSPORT_CHUNK = 5

FIRST_SAMPLES = bytes([0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23])
SECOND_SAMPLES = bytes([0x30, 0x31, 0x32, 0x33])


def main() -> int:
    first = CODEC.encode("SensorPacket", {"version": 2, "sample_count": 2, "sample_bytes": FIRST_SAMPLES})
    second = CODEC.encode("SensorPacket", {"version": 2, "sample_count": 1, "sample_bytes": SECOND_SAMPLES})

    # Build the on-wire stream: each packet wrapped in its length prefix.
    stream = encode_frame(first, prefix_width=PREFIX_WIDTH) + encode_frame(second, prefix_width=PREFIX_WIDTH)

    decoder = FrameDecoder(prefix_width=PREFIX_WIDTH)
    decoded_packets = 0
    transport_reads = 0

    # Feed the stream in small chunks, exactly like reading from a socket/pipe.
    for offset in range(0, len(stream), TRANSPORT_CHUNK):
        chunk = stream[offset:offset + TRANSPORT_CHUNK]
        transport_reads += 1
        for frame in decoder.feed(chunk):
            message = CODEC.decode("SensorPacket", frame)
            print(
                f"hardware_stream packet={decoded_packets} "
                f"sample_count={message['sample_count']} sample_bytes_len={message['sample_bytes_len']}"
            )
            decoded_packets += 1

    print(
        f"hardware_stream_stats frames_decoded={decoded_packets} "
        f"transport_reads={transport_reads} bytes_read={len(stream)}"
    )
    return 0 if decoded_packets == 2 else 1


if __name__ == "__main__":
    raise SystemExit(main())
