"""Universal Protocol Runtime - pure-Python codec, framing and session helpers.

This package provides a dependency-free encoder/decoder driven by schema
descriptors emitted by ``upr-gen --lang python``, plus framing/session helpers
that interoperate with the C++ runtime.

Typical usage with a generated module ``my_protocol``::

    from my_protocol import CODEC
    frame = CODEC.encode("Quote", {"price": 100, "size": 5})
    values = CODEC.decode("Quote", frame)
"""

from __future__ import annotations

from . import checksums, codec, framing, metadata
from .errors import DecodeError, DecodeStatus, EncodeError, UprError
from .framing import (
    FrameDecoder,
    FramingError,
    Handshake,
    TransportMode,
    check_compatibility,
    decode_handshake,
    encode_frame,
    encode_handshake,
    iter_frames,
    try_read_frame,
)
from .metadata import (
    Checksum,
    ChecksumAnchor,
    Field,
    Layout,
    Protocol,
    VariantCase,
)
from .runtime import Codec

__version__ = "0.1.0"

__all__ = [
    "Codec",
    "Checksum",
    "ChecksumAnchor",
    "DecodeError",
    "DecodeStatus",
    "EncodeError",
    "Field",
    "FrameDecoder",
    "FramingError",
    "Handshake",
    "Layout",
    "Protocol",
    "TransportMode",
    "UprError",
    "VariantCase",
    "checksums",
    "check_compatibility",
    "codec",
    "decode_handshake",
    "encode_frame",
    "encode_handshake",
    "framing",
    "iter_frames",
    "metadata",
    "try_read_frame",
    "__version__",
]
