"""Dependency-free, metadata-driven encoder/decoder for UPR protocols.

The algorithm here mirrors the C++ direct codec (and therefore the dynamic
decoder) byte-for-byte: field ordering, alignment padding, presence/condition
gating, collections, tagged variants, reserved fills, expected constants and
checksums all match, so frames are interchangeable across languages.

Values are plain Python objects:

* scalars            -> ``int``
* ``float32/float64``-> ``float``
* ``bytes``          -> ``bytes``
* ``string``         -> ``str``
* ``struct``         -> ``dict``
* ``collection``     -> ``list[dict]``
* ``variant``        -> ``dict`` (the active case's fields)

Length/count fields (``size_from_field`` / ``count_from_field``) are derived
automatically from the data on encode, so callers do not have to keep them in
sync by hand.
"""

from __future__ import annotations

import struct as _struct
from typing import Any, Dict, List, Mapping, Optional, Tuple

from . import checksums
from .errors import DecodeError, DecodeStatus, EncodeError
from .metadata import Checksum, ChecksumAnchor, Field, Layout, Protocol

_SCALAR_KINDS = {"unsigned", "signed", "float32", "float64", "enum"}


def _byteorder(order: str) -> str:
    return "little" if order == "little_endian" else "big"


def _align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    remainder = value % alignment
    return value if remainder == 0 else value + (alignment - remainder)


def _validate_ascii(data: bytes) -> bool:
    return all(byte < 0x80 for byte in data)


def _validate_utf8(data: bytes) -> bool:
    try:
        data.decode("utf-8")
        return True
    except UnicodeDecodeError:
        return False


# --------------------------------------------------------------------------- #
# Encoding
# --------------------------------------------------------------------------- #


def _derived_length_fields(layout: Layout, values: Mapping[str, Any]) -> Dict[int, int]:
    """Computes auto-derived size/count field values from the payload."""
    derived: Dict[int, int] = {}
    for fld in layout.fields:
        if fld.is_gated and not _field_present(layout, fld, values):
            continue
        if fld.kind in ("bytes", "string") and fld.dynamic_size:
            value = values.get(fld.name)
            if value is None:
                continue
            payload = value.encode(fld.string_encoding if fld.string_encoding != "ascii" else "ascii") if isinstance(value, str) else bytes(value)
            derived[fld.size_from_field] = len(payload)
        elif fld.kind == "collection" and fld.dynamic_count:
            value = values.get(fld.name)
            if value is not None:
                derived[fld.count_from_field] = len(value)
    return derived


def _field_present(layout: Layout, fld: Field, values: Mapping[str, Any]) -> bool:
    if fld.has_condition:
        other = layout.fields[fld.condition_field]
        return int(values.get(other.name, 0)) == fld.condition_equals
    if fld.has_presence:
        other = layout.fields[fld.presence_field]
        return ((int(values.get(other.name, 0)) >> fld.presence_bit) & 1) != 0
    return True


def _encode_scalar(out: bytearray, fld: Field, value: Any) -> None:
    order = _byteorder(fld.byte_order)
    try:
        if fld.kind in ("unsigned", "enum"):
            out += int(value).to_bytes(fld.width_bytes, order, signed=False)
        elif fld.kind == "signed":
            out += int(value).to_bytes(fld.width_bytes, order, signed=True)
        elif fld.kind == "float32":
            out += _struct.pack(("<f" if order == "little" else ">f"), float(value))
        elif fld.kind == "float64":
            out += _struct.pack(("<d" if order == "little" else ">d"), float(value))
        else:  # pragma: no cover - guarded by caller
            raise EncodeError(f"unsupported scalar kind {fld.kind}", fld.name)
    except (OverflowError, ValueError, _struct.error) as exc:
        raise EncodeError(str(exc), fld.name) from exc


def _encode_field(
    protocol: Protocol,
    out: bytearray,
    layout: Layout,
    fld: Field,
    value: Any,
    values: Mapping[str, Any],
) -> None:
    if fld.kind in _SCALAR_KINDS:
        _encode_scalar(out, fld, value)
        return
    if fld.kind in ("bytes", "string"):
        if isinstance(value, str):
            payload = value.encode("utf-8" if fld.string_encoding == "utf8" else "ascii")
        else:
            payload = bytes(value)
        if not fld.dynamic_size and len(payload) != fld.fixed_size:
            raise EncodeError(
                f"fixed field expects {fld.fixed_size} bytes, got {len(payload)}", fld.name
            )
        out += payload
        return
    if fld.kind == "struct":
        nested = protocol.struct_by_id(fld.struct_id)
        out += _encode_layout(protocol, nested, value or {})
        return
    if fld.kind == "collection":
        nested = protocol.struct_by_id(fld.struct_id)
        for element in value or []:
            out += _encode_layout(protocol, nested, element)
        return
    if fld.kind == "variant":
        # The active case is selected by the (already-encoded) tag field.
        tag_field = layout.fields[fld.tag_from_field]
        if tag_field.name not in values:
            raise EncodeError("missing variant tag value", tag_field.name)
        tag_value = int(values[tag_field.name])
        for case in fld.variant_cases:
            if case.tag_value == tag_value:
                nested = protocol.struct_by_id(case.struct_id)
                out += _encode_layout(protocol, nested, value or {})
                return
        raise EncodeError(f"no variant case for tag {tag_value}", fld.name)
    raise EncodeError(f"unsupported field kind {fld.kind}", fld.name)


def _is_checksum_field(layout: Layout, field_id: int) -> bool:
    return any(chk.field_id == field_id for chk in layout.checksums)


def _encode_layout(protocol: Protocol, layout: Layout, values: Mapping[str, Any]) -> bytes:
    out = bytearray()
    derived = _derived_length_fields(layout, values)
    field_starts: Dict[int, int] = {}
    field_ends: Dict[int, int] = {}

    for fld in layout.fields:
        if fld.is_gated and not _field_present(layout, fld, values):
            field_starts[fld.id] = len(out)
            field_ends[fld.id] = len(out)
            continue
        aligned = _align_up(len(out), fld.alignment)
        out += b"\x00" * (aligned - len(out))
        field_starts[fld.id] = len(out)

        if _is_checksum_field(layout, fld.id):
            out += b"\x00" * fld.width_bytes
        elif fld.id in derived:
            _encode_scalar(out, fld, derived[fld.id])
        elif fld.has_expected_unsigned:
            _encode_scalar(out, fld, fld.expected_unsigned)
        elif fld.is_reserved:
            out += bytes([fld.reserved_fill_byte]) * fld.fixed_size
        else:
            if fld.name not in values:
                raise EncodeError("missing value", fld.name)
            _encode_field(protocol, out, layout, fld, values[fld.name], values)
        field_ends[fld.id] = len(out)

    for chk in layout.checksums:
        frm = _anchor_offset(chk.from_anchor, field_starts, field_ends, len(out))
        to = _anchor_offset(chk.to_anchor, field_starts, field_ends, len(out))
        try:
            digest = checksums.compute(chk.algorithm_name, memoryview(out)[frm:to])
        except KeyError as exc:
            raise EncodeError(f"unknown checksum algorithm '{chk.algorithm_name}'") from exc
        chk_field = layout.fields[chk.field_id]
        order = _byteorder(chk_field.byte_order)
        start = field_starts[chk.field_id]
        out[start:start + chk_field.width_bytes] = digest.to_bytes(chk_field.width_bytes, order)

    return bytes(out)


def _anchor_offset(
    anchor: ChecksumAnchor, starts: Mapping[int, int], ends: Mapping[int, int], total: int
) -> int:
    if anchor.kind == "frame_start":
        return 0
    if anchor.kind == "frame_end":
        return total
    if anchor.kind in ("field_start", "before_self"):
        return starts[anchor.field_id]
    if anchor.kind in ("field_end", "after_self"):
        return ends[anchor.field_id]
    raise EncodeError(f"unknown checksum anchor {anchor.kind}")


def encode(protocol: Protocol, layout: Layout, values: Mapping[str, Any]) -> bytes:
    """Encodes a mapping of field values into a wire frame.

    Args:
        protocol: The owning protocol (for struct resolution).
        layout: The message/struct layout to encode.
        values: Mapping of field name to value.

    Returns:
        The encoded frame bytes.

    Raises:
        EncodeError: If a value is missing or inconsistent with the schema.
    """
    return _encode_layout(protocol, layout, values)


# --------------------------------------------------------------------------- #
# Decoding
# --------------------------------------------------------------------------- #


def _read_scalar(frame: bytes, offset: int, fld: Field) -> Any:
    order = _byteorder(fld.byte_order)
    raw = frame[offset:offset + fld.width_bytes]
    if fld.kind in ("unsigned", "enum"):
        return int.from_bytes(raw, order, signed=False)
    if fld.kind == "signed":
        return int.from_bytes(raw, order, signed=True)
    if fld.kind == "float32":
        return _struct.unpack(("<f" if order == "little" else ">f"), raw)[0]
    if fld.kind == "float64":
        return _struct.unpack(("<d" if order == "little" else ">d"), raw)[0]
    raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, offset)


def _decode_layout(
    protocol: Protocol,
    layout: Layout,
    frame: bytes,
    base: int,
    bounded: bool,
) -> Tuple[Dict[str, Any], int]:
    """Decodes one layout from ``frame[base:]``.

    Args:
        bounded: When True the layout must consume exactly the rest of the frame
            (top-level message without trailing bytes). When False trailing
            bytes are tolerated (nested struct / sequence element).

    Returns:
        Tuple of (decoded mapping, bytes consumed).
    """
    out: Dict[str, Any] = {}
    offset = 0
    field_starts: Dict[int, int] = {}
    field_ends: Dict[int, int] = {}
    available = len(frame) - base

    if available < layout.minimum_size:
        raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", base)
    if layout.is_message and layout.dispatch_prefix:
        if frame[base:base + len(layout.dispatch_prefix)] != layout.dispatch_prefix:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", base)

    for fld in layout.fields:
        if fld.is_gated and not _decoded_present(layout, fld, out):
            field_starts[fld.id] = offset
            field_ends[fld.id] = offset
            continue
        offset = _align_up(offset, fld.alignment)
        field_starts[fld.id] = offset
        offset = _decode_field(protocol, layout, fld, frame, base, offset, out)
        field_ends[fld.id] = offset

    if layout.is_message:
        if not layout.allow_trailing_bytes and offset != available:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", base + offset)
        checksum_limit = available if layout.allow_trailing_bytes else offset
    else:
        if bounded and offset != available:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", base + offset)
        checksum_limit = offset

    for chk in layout.checksums:
        try:
            frm = _anchor_offset(chk.from_anchor, field_starts, field_ends, checksum_limit)
            to = _anchor_offset(chk.to_anchor, field_starts, field_ends, checksum_limit)
        except EncodeError as exc:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, layout.fields[chk.field_id].name, base) from exc
        chk_offset = base + field_starts[chk.field_id]
        if frm > to or to > checksum_limit:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, layout.fields[chk.field_id].name, chk_offset)
        try:
            digest = checksums.compute(chk.algorithm_name, memoryview(frame)[base + frm:base + to])
        except KeyError as exc:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, layout.fields[chk.field_id].name, chk_offset) from exc
        if int(out[layout.fields[chk.field_id].name]) != digest:
            raise DecodeError(DecodeStatus.CHECKSUM_MISMATCH, layout.fields[chk.field_id].name, chk_offset)

    return out, offset


def _decoded_present(layout: Layout, fld: Field, out: Mapping[str, Any]) -> bool:
    if fld.has_condition:
        other = layout.fields[fld.condition_field]
        return int(out.get(other.name, 0)) == fld.condition_equals
    if fld.has_presence:
        other = layout.fields[fld.presence_field]
        return ((int(out.get(other.name, 0)) >> fld.presence_bit) & 1) != 0
    return True


def _decode_field(
    protocol: Protocol,
    layout: Layout,
    fld: Field,
    frame: bytes,
    base: int,
    offset: int,
    out: Dict[str, Any],
) -> int:
    available = len(frame) - base

    if fld.kind in _SCALAR_KINDS:
        if offset + fld.width_bytes > available:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
        value = _read_scalar(frame, base + offset, fld)
        if fld.has_expected_unsigned and int(value) != fld.expected_unsigned:
            raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)
        out[fld.name] = value
        return offset + fld.width_bytes

    if fld.kind in ("bytes", "string"):
        size = int(out[layout.fields[fld.size_from_field].name]) if fld.dynamic_size else fld.fixed_size
        if offset + size > available:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
        raw = frame[base + offset:base + offset + size]
        if fld.kind == "string":
            valid = _validate_utf8(raw) if fld.string_encoding == "utf8" else _validate_ascii(raw)
            if not valid:
                raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)
            out[fld.name] = raw.decode("utf-8" if fld.string_encoding == "utf8" else "ascii")
        else:
            if fld.is_reserved:
                if any(byte != fld.reserved_fill_byte for byte in raw):
                    raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)
            out[fld.name] = raw
        return offset + size

    if fld.kind == "struct":
        nested = protocol.struct_by_id(fld.struct_id)
        try:
            value, consumed = _decode_layout(protocol, nested, frame, base + offset, bounded=False)
        except DecodeError as exc:
            raise DecodeError(exc.status, _prefix(fld.name, exc.field_name), exc.byte_offset) from None
        out[fld.name] = value
        return offset + consumed

    if fld.kind == "collection":
        nested = protocol.struct_by_id(fld.struct_id)
        count = int(out[layout.fields[fld.count_from_field].name]) if fld.dynamic_count else fld.fixed_count
        if nested.minimum_size > 0 and count > (available - offset) // nested.minimum_size:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
        elements: List[Dict[str, Any]] = []
        for index in range(count):
            if base + offset > len(frame):
                raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
            try:
                value, consumed = _decode_layout(protocol, nested, frame, base + offset, bounded=False)
            except DecodeError as exc:
                raise DecodeError(
                    exc.status, _prefix(f"{fld.name}[{index}]", exc.field_name), exc.byte_offset
                ) from None
            if consumed == 0:
                raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
            offset += consumed
            elements.append(value)
        out[fld.name] = elements
        return offset

    if fld.kind == "variant":
        tag = int(out[layout.fields[fld.tag_from_field].name])
        for case in fld.variant_cases:
            if case.tag_value == tag:
                nested = protocol.struct_by_id(case.struct_id)
                try:
                    value, consumed = _decode_layout(protocol, nested, frame, base + offset, bounded=False)
                except DecodeError as exc:
                    raise DecodeError(exc.status, _prefix(fld.name, exc.field_name), exc.byte_offset) from None
                out[fld.name] = value
                return offset + consumed
        raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)

    raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)


def _prefix(parent: str, child: str) -> str:
    if not child:
        return parent
    return f"{parent}.{child}"


def decode(protocol: Protocol, layout: Layout, frame: bytes) -> Dict[str, Any]:
    """Decodes a single frame into a mapping of field values.

    Raises:
        DecodeError: With rich field/offset context on failure.
    """
    data = frame if isinstance(frame, bytes) else bytes(frame)
    values, _ = _decode_layout(protocol, layout, data, 0, bounded=True)
    return values


def decode_sequence(protocol: Protocol, layout: Layout, frame: bytes) -> List[Dict[str, Any]]:
    """Decodes a packed sequence of records, tracking consumption internally.

    Most useful for struct records (length-bounded per element). Returns the
    list of decoded mappings.
    """
    data = frame if isinstance(frame, bytes) else bytes(frame)
    records: List[Dict[str, Any]] = []
    offset = 0
    while offset < len(data):
        value, consumed = _decode_layout(protocol, layout, data, offset, bounded=False)
        if consumed == 0:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", offset)
        offset += consumed
        records.append(value)
    return records
