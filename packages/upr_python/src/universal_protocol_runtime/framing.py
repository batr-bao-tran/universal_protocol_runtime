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
_BYTEORDER_LITTLE = "little"
_DEFAULT_PREFIX_WIDTH = 4
_SUPPORTED_PREFIX_WIDTHS = (1, 2, 4)
_DEFAULT_MAX_FRAME_BYTES = 1 << 20
_HANDSHAKE_MAGIC_OFFSET = 0
_HANDSHAKE_MAGIC_SIZE = 4
_HANDSHAKE_PROTOCOL_VERSION_OFFSET = 4
_HANDSHAKE_FLAGS_OFFSET = 6
_HANDSHAKE_TRANSPORT_MODE_OFFSET = 8
_HANDSHAKE_FRAME_CODEC_OFFSET = 10
_HANDSHAKE_MAX_FRAME_BYTES_OFFSET = 12
_HANDSHAKE_SESSION_ID_OFFSET = 16
_UINT16_WIDTH = 2
_UINT32_WIDTH = 4
_UINT64_WIDTH = 8
_FRAME_DECODER_COMPACT_RATIO = 2


class FramingError(UprError):
    """Raised on malformed framing or handshake data."""


def _validate_prefix_width(prefix_width: int) -> None:
    if prefix_width not in _SUPPORTED_PREFIX_WIDTHS:
        raise FramingError(f"unsupported prefix width {prefix_width}")


def encode_frame(
    payload: bytes, *, prefix_width: int = _DEFAULT_PREFIX_WIDTH, max_payload: int = _DEFAULT_MAX_FRAME_BYTES
) -> bytes:
    """Wraps a payload in a little-endian length prefix.

    Args:
        payload: The frame payload bytes.
        prefix_width: Prefix width in bytes (1, 2 or 4).
        max_payload: Maximum allowed payload size.

    Returns:
        The framed bytes (prefix followed by payload).
    """
    _validate_prefix_width(prefix_width)
    if len(payload) > max_payload:
        raise FramingError("payload exceeds max frame size")
    frame = bytearray(prefix_width + len(payload))
    frame[:prefix_width] = len(payload).to_bytes(prefix_width, _BYTEORDER_LITTLE)
    frame[prefix_width:] = payload
    return bytes(frame)


def try_read_frame(
    buffer: bytes, *, prefix_width: int = _DEFAULT_PREFIX_WIDTH, max_payload: int = _DEFAULT_MAX_FRAME_BYTES
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
    _validate_prefix_width(prefix_width)
    if len(buffer) < prefix_width:
        return None
    view = memoryview(buffer)
    payload_size = int.from_bytes(view[:prefix_width], _BYTEORDER_LITTLE)
    if payload_size > max_payload:
        raise FramingError("frame exceeds configured max frame size")
    total = prefix_width + payload_size
    if len(buffer) < total:
        return None
    return view[prefix_width:total].tobytes(), total


def iter_frames(
    buffer: bytes, *, prefix_width: int = _DEFAULT_PREFIX_WIDTH, max_payload: int = _DEFAULT_MAX_FRAME_BYTES
) -> Iterator[bytes]:
    """Yields every complete frame contained in ``buffer``."""
    _validate_prefix_width(prefix_width)
    view = memoryview(buffer)
    offset = 0
    while True:
        if len(view) - offset < prefix_width:
            return
        payload_size = int.from_bytes(view[offset:offset + prefix_width], _BYTEORDER_LITTLE)
        if payload_size > max_payload:
            raise FramingError("frame exceeds configured max frame size")
        consumed = prefix_width + payload_size
        if len(view) - offset < consumed:
            return
        payload = view[offset + prefix_width:offset + consumed].tobytes()
        offset += consumed
        yield payload


class FrameDecoder:
    """Stateful accumulator that yields frames as bytes arrive."""

    def __init__(self, *, prefix_width: int = _DEFAULT_PREFIX_WIDTH, max_payload: int = _DEFAULT_MAX_FRAME_BYTES) -> None:
        _validate_prefix_width(prefix_width)
        self._prefix_width = prefix_width
        self._max_payload = max_payload
        self._buffer = bytearray()
        self._read_offset = 0
        self._write_offset = 0

    def _append(self, data: bytes) -> None:
        if not data:
            return
        unread = self._write_offset - self._read_offset
        if self._read_offset and self._read_offset * _FRAME_DECODER_COMPACT_RATIO >= len(self._buffer):
            del self._buffer[:self._read_offset]
            self._read_offset = 0
            self._write_offset = unread
        self._buffer.extend(data)
        self._write_offset += len(data)

    def _available_view(self) -> memoryview:
        return memoryview(self._buffer)[self._read_offset:self._write_offset]

    def feed(self, data: bytes) -> List[bytes]:
        """Feeds received bytes and returns any newly completed frames."""
        self._append(data)
        frames: List[bytes] = []
        view = self._available_view()
        offset = 0
        while True:
            if len(view) - offset < self._prefix_width:
                break
            payload_size = int.from_bytes(view[offset:offset + self._prefix_width], _BYTEORDER_LITTLE)
            if payload_size > self._max_payload:
                raise FramingError("frame exceeds configured max frame size")
            consumed = self._prefix_width + payload_size
            if len(view) - offset < consumed:
                break
            payload = view[offset + self._prefix_width:offset + consumed].tobytes()
            frames.append(payload)
            offset += consumed
        self._read_offset += offset
        del view
        if self._read_offset == self._write_offset:
            self._buffer.clear()
            self._read_offset = 0
            self._write_offset = 0
        return frames


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
    max_frame_bytes: int = _DEFAULT_MAX_FRAME_BYTES
    session_id: int = 0


def encode_handshake(handshake: Handshake) -> bytes:
    """Encodes a :class:`Handshake` into its 24-byte payload."""
    out = bytearray(HANDSHAKE_SIZE)
    out[_HANDSHAKE_MAGIC_OFFSET:_HANDSHAKE_MAGIC_SIZE] = _HANDSHAKE_MAGIC
    out[_HANDSHAKE_PROTOCOL_VERSION_OFFSET:_HANDSHAKE_FLAGS_OFFSET] = int(handshake.protocol_version).to_bytes(
        _UINT16_WIDTH, _BYTEORDER_LITTLE
    )
    out[_HANDSHAKE_FLAGS_OFFSET:_HANDSHAKE_TRANSPORT_MODE_OFFSET] = int(handshake.flags).to_bytes(
        _UINT16_WIDTH, _BYTEORDER_LITTLE
    )
    out[_HANDSHAKE_TRANSPORT_MODE_OFFSET:_HANDSHAKE_FRAME_CODEC_OFFSET] = int(handshake.transport_mode).to_bytes(
        _UINT16_WIDTH, _BYTEORDER_LITTLE
    )
    out[_HANDSHAKE_FRAME_CODEC_OFFSET:_HANDSHAKE_MAX_FRAME_BYTES_OFFSET] = int(handshake.frame_codec).to_bytes(
        _UINT16_WIDTH, _BYTEORDER_LITTLE
    )
    out[_HANDSHAKE_MAX_FRAME_BYTES_OFFSET:_HANDSHAKE_SESSION_ID_OFFSET] = int(handshake.max_frame_bytes).to_bytes(
        _UINT32_WIDTH, _BYTEORDER_LITTLE
    )
    out[_HANDSHAKE_SESSION_ID_OFFSET:HANDSHAKE_SIZE] = int(handshake.session_id).to_bytes(
        _UINT64_WIDTH, _BYTEORDER_LITTLE
    )
    return bytes(out)


def decode_handshake(payload: bytes) -> Handshake:
    """Decodes a 24-byte handshake payload into a :class:`Handshake`.

    Raises:
        FramingError: If the payload size or magic is invalid.
    """
    if len(payload) != HANDSHAKE_SIZE:
        raise FramingError("UPR handshake frame has an unexpected size")
    if payload[_HANDSHAKE_MAGIC_OFFSET:_HANDSHAKE_MAGIC_SIZE] != _HANDSHAKE_MAGIC:
        raise FramingError("UPR handshake magic is invalid")
    transport_value = int.from_bytes(
        payload[_HANDSHAKE_TRANSPORT_MODE_OFFSET:_HANDSHAKE_FRAME_CODEC_OFFSET], _BYTEORDER_LITTLE
    )
    try:
        transport_mode = TransportMode(transport_value)
    except ValueError as exc:
        raise FramingError("UPR handshake transport mode is invalid") from exc
    return Handshake(
        protocol_version=int.from_bytes(
            payload[_HANDSHAKE_PROTOCOL_VERSION_OFFSET:_HANDSHAKE_FLAGS_OFFSET], _BYTEORDER_LITTLE
        ),
        flags=int.from_bytes(payload[_HANDSHAKE_FLAGS_OFFSET:_HANDSHAKE_TRANSPORT_MODE_OFFSET], _BYTEORDER_LITTLE),
        transport_mode=transport_mode,
        frame_codec=int.from_bytes(
            payload[_HANDSHAKE_FRAME_CODEC_OFFSET:_HANDSHAKE_MAX_FRAME_BYTES_OFFSET], _BYTEORDER_LITTLE
        ),
        max_frame_bytes=int.from_bytes(
            payload[_HANDSHAKE_MAX_FRAME_BYTES_OFFSET:_HANDSHAKE_SESSION_ID_OFFSET], _BYTEORDER_LITTLE
        ),
        session_id=int.from_bytes(payload[_HANDSHAKE_SESSION_ID_OFFSET:HANDSHAKE_SIZE], _BYTEORDER_LITTLE),
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
