"""Length-prefixed framing and session handshake helpers.

These match the C++ ``FrameChannel`` and ``UprSession`` wire formats so a Python
peer can interoperate with a C++ peer. See ``docs/WIRE_SPEC.md`` for the
normative byte layout.

Frame layout (stream transports)::

    [ length prefix : little-endian, prefix_width bytes ][ payload ... ]

The length prefix encodes the payload size in bytes (the prefix itself is not
counted). The default prefix width is 4 bytes.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import Iterator, List, Optional, Tuple

from .errors import UprError

_HANDSHAKE_MAGIC = b"UPR1"
HANDSHAKE_SIZE = 24


class FramingError(UprError):
    """Raised on malformed framing or handshake data."""


def encode_frame(payload: bytes, *, prefix_width: int = 4, max_payload: int = 1 << 20) -> bytes:
    """Wraps a payload in a little-endian length prefix.

    Args:
        payload: The frame payload bytes.
        prefix_width: Prefix width in bytes (1, 2 or 4).
        max_payload: Maximum allowed payload size.

    Returns:
        The framed bytes (prefix followed by payload).
    """
    if prefix_width not in (1, 2, 4):
        raise FramingError(f"unsupported prefix width {prefix_width}")
    if len(payload) > max_payload:
        raise FramingError("payload exceeds max frame size")
    return len(payload).to_bytes(prefix_width, "little") + payload


def try_read_frame(
    buffer: bytes, *, prefix_width: int = 4, max_payload: int = 1 << 20
) -> Optional[Tuple[bytes, int]]:
    """Attempts to read a single frame from the front of ``buffer``.

    Args:
        buffer: Bytes accumulated from the transport.
        prefix_width: Prefix width in bytes (1, 2 or 4).
        max_payload: Maximum allowed payload size.

    Returns:
        ``(payload, bytes_consumed)`` when a full frame is available, else
        ``None`` (need more data).
    """
    if prefix_width not in (1, 2, 4):
        raise FramingError(f"unsupported prefix width {prefix_width}")
    if len(buffer) < prefix_width:
        return None
    payload_size = int.from_bytes(buffer[:prefix_width], "little")
    if payload_size > max_payload:
        raise FramingError("frame exceeds configured max frame size")
    total = prefix_width + payload_size
    if len(buffer) < total:
        return None
    return buffer[prefix_width:total], total


def iter_frames(buffer: bytes, *, prefix_width: int = 4, max_payload: int = 1 << 20) -> Iterator[bytes]:
    """Yields every complete frame contained in ``buffer``."""
    offset = 0
    while True:
        result = try_read_frame(buffer[offset:], prefix_width=prefix_width, max_payload=max_payload)
        if result is None:
            return
        payload, consumed = result
        offset += consumed
        yield payload


class FrameDecoder:
    """Stateful accumulator that yields frames as bytes arrive."""

    def __init__(self, *, prefix_width: int = 4, max_payload: int = 1 << 20) -> None:
        if prefix_width not in (1, 2, 4):
            raise FramingError(f"unsupported prefix width {prefix_width}")
        self._prefix_width = prefix_width
        self._max_payload = max_payload
        self._buffer = bytearray()

    def feed(self, data: bytes) -> List[bytes]:
        """Feeds received bytes and returns any newly completed frames."""
        self._buffer += data
        frames: List[bytes] = []
        while True:
            if len(self._buffer) < self._prefix_width:
                break
            payload_size = int.from_bytes(self._buffer[:self._prefix_width], "little")
            if payload_size > self._max_payload:
                raise FramingError("frame exceeds configured max frame size")
            consumed = self._prefix_width + payload_size
            if len(self._buffer) < consumed:
                break
            payload = bytes(self._buffer[self._prefix_width:consumed])
            del self._buffer[:consumed]
            frames.append(payload)
        return frames


# --------------------------------------------------------------------------- #
# Session handshake
# --------------------------------------------------------------------------- #


class TransportMode(enum.IntEnum):
    """UPR transport modes negotiated during the handshake."""

    LENGTH_PREFIXED_STREAM = 1
    DESCRIPTOR_RING = 2
    DATAGRAM = 3


@dataclass
class Handshake:
    """Session handshake payload (mirrors ``UprSessionHandshake``)."""

    protocol_version: int = 1
    flags: int = 0
    transport_mode: TransportMode = TransportMode.LENGTH_PREFIXED_STREAM
    frame_codec: int = 1
    max_frame_bytes: int = 1 << 20
    session_id: int = 0


def encode_handshake(handshake: Handshake) -> bytes:
    """Encodes a :class:`Handshake` into its 24-byte payload."""
    out = bytearray(_HANDSHAKE_MAGIC)
    out += int(handshake.protocol_version).to_bytes(2, "little")
    out += int(handshake.flags).to_bytes(2, "little")
    out += int(handshake.transport_mode).to_bytes(2, "little")
    out += int(handshake.frame_codec).to_bytes(2, "little")
    out += int(handshake.max_frame_bytes).to_bytes(4, "little")
    out += int(handshake.session_id).to_bytes(8, "little")
    return bytes(out)


def decode_handshake(payload: bytes) -> Handshake:
    """Decodes a 24-byte handshake payload into a :class:`Handshake`.

    Raises:
        FramingError: If the payload size or magic is invalid.
    """
    if len(payload) != HANDSHAKE_SIZE:
        raise FramingError("UPR handshake frame has an unexpected size")
    if payload[:4] != _HANDSHAKE_MAGIC:
        raise FramingError("UPR handshake magic is invalid")
    transport_value = int.from_bytes(payload[8:10], "little")
    try:
        transport_mode = TransportMode(transport_value)
    except ValueError as exc:
        raise FramingError("UPR handshake transport mode is invalid") from exc
    return Handshake(
        protocol_version=int.from_bytes(payload[4:6], "little"),
        flags=int.from_bytes(payload[6:8], "little"),
        transport_mode=transport_mode,
        frame_codec=int.from_bytes(payload[10:12], "little"),
        max_frame_bytes=int.from_bytes(payload[12:16], "little"),
        session_id=int.from_bytes(payload[16:24], "little"),
    )


def check_compatibility(local: Handshake, remote: Handshake) -> None:
    """Validates that two handshakes are compatible (mirrors the C++ rules).

    Raises:
        FramingError: If versions, transport modes or codecs disagree, or the
            remote max frame size is smaller than the local requirement.
    """
    if local.protocol_version != remote.protocol_version:
        raise FramingError("UPR protocol versions do not match")
    if local.transport_mode != remote.transport_mode:
        raise FramingError("UPR transport modes do not match")
    if local.frame_codec != remote.frame_codec:
        raise FramingError("UPR frame codecs do not match")
    if remote.max_frame_bytes < local.max_frame_bytes:
        raise FramingError("Remote max frame size is smaller than the local requirement")
