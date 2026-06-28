"""Universal Protocol Runtime Python facade, framing and session helpers.

Generated protocol modules emitted by ``upr-gen --lang python`` delegate
encode/decode to their generated pybind11 extension, which wraps the C++
direct/static codec. This package also provides shared metadata types,
exceptions, framing and session helpers.

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
