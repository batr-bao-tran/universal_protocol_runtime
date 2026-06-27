"""Error types shared by the Universal Protocol Runtime Python codec.

These mirror the C++ ``DecodeStatus``/``DecodeError`` and ``EncodeStatus`` types
so that diagnostics are consistent across languages.
"""

from __future__ import annotations

import enum


class DecodeStatus(enum.Enum):
    """Result codes returned by decode operations (mirrors the C++ enum)."""

    OK = "ok"
    MESSAGE_NOT_FOUND = "message_not_found"
    SCHEMA_MISMATCH = "schema_mismatch"
    INVALID_DATA = "invalid_data"
    CHECKSUM_MISMATCH = "checksum_mismatch"
    FIELD_LIMIT_EXCEEDED = "field_limit_exceeded"


class UprError(Exception):
    """Base class for all runtime errors."""


class DecodeError(UprError):
    """Rich decode failure carrying the failing field and byte offset.

    Attributes:
        status: The :class:`DecodeStatus` describing the failure.
        field_name: Dotted path to the field that failed (for example
            ``"pairs[1].value.b"``), or ``""`` when not field specific.
        byte_offset: Byte offset within the frame where decoding failed.
    """

    def __init__(self, status: DecodeStatus, field_name: str = "", byte_offset: int = 0) -> None:
        self.status = status
        self.field_name = field_name
        self.byte_offset = byte_offset
        location = f" at field '{field_name}'" if field_name else ""
        super().__init__(f"decode failed ({status.value}){location} at byte offset {byte_offset}")


class EncodeError(UprError):
    """Raised when a value cannot be encoded against its schema."""

    def __init__(self, message: str, field_name: str = "") -> None:
        self.field_name = field_name
        location = f" at field '{field_name}'" if field_name else ""
        super().__init__(f"encode failed{location}: {message}")
