"""Framing, session handshake and checksum helpers.

The schema-driven examples cover encode/decode. This example rounds out the
remaining runtime surface that interoperates with the C++ ``FrameChannel`` and
``UprSession``:

* length-prefixed framing: ``encode_frame``, ``try_read_frame``, ``iter_frames``
  and the stateful ``FrameDecoder``;
* the ``UPR1`` session handshake: ``encode_handshake`` / ``decode_handshake`` and
  ``check_compatibility``;
* the built-in checksum algorithms.

None of this needs a schema - it operates on raw payload bytes - so it applies
to any protocol you encode with the ``Codec``.
"""

from __future__ import annotations

from universal_protocol_runtime import (
    FrameDecoder,
    FramingError,
    Handshake,
    TransportMode,
    check_compatibility,
    checksums,
    decode_handshake,
    encode_frame,
    encode_handshake,
    iter_frames,
    try_read_frame,
)


def demo_framing() -> None:
    print("== framing ==")
    payloads = [b"alpha", b"bravo", b"charlie"]

    # Default prefix width is 4 bytes (little-endian). Use prefix_width=1/2 to
    # match a tighter wire format such as the hardware stream example.
    wire = b"".join(encode_frame(p) for p in payloads)
    print(f"wire_bytes={len(wire)} first_frame_hex={encode_frame(payloads[0]).hex()}")

    # One-shot: pull every complete frame out of a fully-buffered blob.
    print(f"iter_frames={[bytes(f) for f in iter_frames(wire)]}")

    # Single-frame parse returning (payload, bytes_consumed), or None if the
    # buffer does not yet hold a whole frame.
    head = try_read_frame(wire)
    assert head is not None
    payload, consumed = head
    print(f"try_read_frame payload={bytes(payload)!r} consumed={consumed}")

    # Streaming: a FrameDecoder reassembles frames from arbitrary chunk
    # boundaries (here we split mid-frame on purpose).
    decoder = FrameDecoder()
    received: list[bytes] = []
    for index in range(0, len(wire), 3):
        received.extend(decoder.feed(wire[index:index + 3]))
    print(f"frame_decoder={received}")
    assert received == payloads


def demo_handshake() -> None:
    print("== session handshake ==")
    local = Handshake(
        transport_mode=TransportMode.LENGTH_PREFIXED_STREAM,
        max_frame_bytes=1 << 20,
        session_id=0x1122334455667788,
    )
    blob = encode_handshake(local)
    print(f"handshake_bytes={len(blob)} hex={blob.hex()}")

    parsed = decode_handshake(blob)
    assert parsed == local
    print(f"decoded transport_mode={parsed.transport_mode.name} session_id={parsed.session_id:#x}")

    # A peer that advertises a smaller max frame size is rejected.
    remote_ok = Handshake(max_frame_bytes=1 << 20, session_id=0x99)
    check_compatibility(local, remote_ok)
    print("compatible_peer=ok")

    remote_bad = Handshake(max_frame_bytes=1024, session_id=0x99)
    try:
        check_compatibility(local, remote_bad)
    except FramingError as exc:
        print(f"incompatible_peer rejected: {exc}")


def demo_checksums() -> None:
    print("== checksums ==")
    data = b"123456789"  # the canonical checksum test vector
    for algorithm in ("xor8", "sum16", "crc16_ccitt", "crc32", "crc32c"):
        digest = checksums.compute(algorithm, memoryview(data))
        print(f"{algorithm:>12} = {digest:#010x}")


def main() -> int:
    demo_framing()
    demo_handshake()
    demo_checksums()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
