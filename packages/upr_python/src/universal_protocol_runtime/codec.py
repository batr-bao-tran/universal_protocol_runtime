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

from dataclasses import dataclass, fields as _dataclass_fields, is_dataclass as _is_dataclass
import struct as _struct
from typing import Dict, List, Mapping, Sequence, Tuple, Union, cast

from . import checksums
from .errors import DecodeError, DecodeStatus, EncodeError
from .metadata import ChecksumAnchor, Field, Layout, Protocol

_SCALAR_KINDS = {"unsigned", "signed", "float32", "float64", "enum"}
_LITTLE_ENDIAN = "little"
_BIG_ENDIAN = "big"
_ASCII_ENCODING = "ascii"
_UTF8_ENCODING = "utf-8"
_UTF8_FIELD_ENCODING = "utf8"
_ZERO_FILL = 0
_FLOAT32_FORMATS = {
    _LITTLE_ENDIAN: "<f",
    _BIG_ENDIAN: ">f",
}
_FLOAT64_FORMATS = {
    _LITTLE_ENDIAN: "<d",
    _BIG_ENDIAN: ">d",
}

BytesLike = Union[bytes, bytearray, memoryview]
ScalarValue = Union[int, float]
DecodedMapping = Dict[str, object]


def _byteorder(order: str) -> str:
    return _LITTLE_ENDIAN if order == "little_endian" else _BIG_ENDIAN


def _align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    remainder = value % alignment
    return value if remainder == 0 else value + (alignment - remainder)


def _decode_text(data: memoryview, encoding: str) -> str:
    try:
        return data.tobytes().decode(_UTF8_ENCODING if encoding == _UTF8_FIELD_ENCODING else _ASCII_ENCODING)
    except UnicodeDecodeError:
        raise DecodeError(DecodeStatus.INVALID_DATA) from None


def _payload_bytes(fld: Field, value: object) -> bytes:
    if isinstance(value, str):
        return value.encode(_UTF8_ENCODING if fld.string_encoding == _UTF8_FIELD_ENCODING else _ASCII_ENCODING)
    if isinstance(value, (bytes, bytearray, memoryview)):
        return bytes(value)
    return bytes(cast(Sequence[int], value))


def _value_mapping(value: object, field_name: str = "") -> Mapping[str, object]:
    if isinstance(value, Mapping):
        return value
    if _is_dataclass(value) and not isinstance(value, type):
        return {field.name: getattr(value, field.name) for field in _dataclass_fields(value)}
    raise EncodeError("expected mapping or dataclass", field_name)


def _extend_repeated(out: bytearray, byte: int, count: int) -> None:
    if count <= 0:
        return
    # ``bytes(count)`` builds the zero-fill run in C without an intermediate list,
    # which is the common case (alignment padding); fall back for non-zero fills.
    out.extend(bytes(count) if byte == _ZERO_FILL else bytes([byte]) * count)


@dataclass(frozen=True)
class _DerivedFields:
    lengths: Dict[int, int]
    payloads: Dict[int, bytes]


def _derived_fields(layout: Layout, values: Mapping[str, object]) -> _DerivedFields:
    """Computes auto-derived size/count field values from the payload."""
    lengths: Dict[int, int] = {}
    payloads: Dict[int, bytes] = {}
    for fld in layout.fields:
        if fld.is_gated and not _field_present(layout, fld, values):
            continue
        if fld.kind in ("bytes", "string") and fld.dynamic_size:
            value = values.get(fld.name)
            if value is None:
                continue
            payload = _payload_bytes(fld, value)
            payloads[fld.id] = payload
            lengths[fld.size_from_field] = len(payload)
        elif fld.kind == "collection" and fld.dynamic_count:
            value = values.get(fld.name)
            if value is not None:
                lengths[fld.count_from_field] = len(cast(Sequence[object], value))
    return _DerivedFields(lengths=lengths, payloads=payloads)


def _field_present(layout: Layout, fld: Field, values: Mapping[str, object]) -> bool:
    if fld.has_condition:
        other = layout.fields[fld.condition_field]
        return int(values.get(other.name, 0)) == fld.condition_equals
    if fld.has_presence:
        other = layout.fields[fld.presence_field]
        return ((int(values.get(other.name, 0)) >> fld.presence_bit) & 1) != 0
    return True


def _encode_scalar(out: bytearray, fld: Field, value: object) -> None:
    order = _byteorder(fld.byte_order)
    try:
        if fld.kind in ("unsigned", "enum"):
            out += int(cast(ScalarValue, value)).to_bytes(fld.width_bytes, order, signed=False)
        elif fld.kind == "signed":
            out += int(cast(ScalarValue, value)).to_bytes(fld.width_bytes, order, signed=True)
        elif fld.kind == "float32":
            out += _struct.pack(_FLOAT32_FORMATS[order], float(cast(ScalarValue, value)))
        elif fld.kind == "float64":
            out += _struct.pack(_FLOAT64_FORMATS[order], float(cast(ScalarValue, value)))
        else:  # pragma: no cover - guarded by caller
            raise EncodeError(f"unsupported scalar kind {fld.kind}", fld.name)
    except (OverflowError, ValueError, _struct.error) as exc:
        raise EncodeError(str(exc), fld.name) from exc


def _encode_field(
    protocol: Protocol,
    out: bytearray,
    layout: Layout,
    fld: Field,
    value: object,
    values: Mapping[str, object],
    derived: _DerivedFields,
) -> None:
    if fld.kind in _SCALAR_KINDS:
        _encode_scalar(out, fld, value)
        return
    if fld.kind in ("bytes", "string"):
        payload = derived.payloads[fld.id] if fld.id in derived.payloads else _payload_bytes(fld, value)
        if not fld.dynamic_size and len(payload) != fld.fixed_size:
            raise EncodeError(
                f"fixed field expects {fld.fixed_size} bytes, got {len(payload)}", fld.name
            )
        out.extend(payload)
        return
    if fld.kind == "struct":
        nested = protocol.struct_by_id(fld.struct_id)
        _encode_layout_into(protocol, nested, _value_mapping(value or {}, fld.name), out)
        return
    if fld.kind == "collection":
        nested = protocol.struct_by_id(fld.struct_id)
        for element in cast(Sequence[object], value or []):
            _encode_layout_into(protocol, nested, _value_mapping(element, fld.name), out)
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
                _encode_layout_into(protocol, nested, _value_mapping(value or {}, fld.name), out)
                return
        raise EncodeError(f"no variant case for tag {tag_value}", fld.name)
    raise EncodeError(f"unsupported field kind {fld.kind}", fld.name)


def _encode_layout_into(protocol: Protocol, layout: Layout, values: Mapping[str, object], out: bytearray) -> None:
    layout_start = len(out)
    derived = _derived_fields(layout, values)
    # Per-field offset bookkeeping is only needed to resolve checksum anchors, so
    # skip the dict churn entirely for the (common) checksum-free layouts.
    track_offsets = bool(layout.checksums)
    checksum_field_ids = {chk.field_id for chk in layout.checksums} if track_offsets else frozenset()
    field_starts: Dict[int, int] = {}
    field_ends: Dict[int, int] = {}

    for fld in layout.fields:
        if fld.is_gated and not _field_present(layout, fld, values):
            if track_offsets:
                relative_offset = len(out) - layout_start
                field_starts[fld.id] = relative_offset
                field_ends[fld.id] = relative_offset
            continue
        aligned = layout_start + _align_up(len(out) - layout_start, fld.alignment)
        _extend_repeated(out, _ZERO_FILL, aligned - len(out))
        if track_offsets:
            field_starts[fld.id] = len(out) - layout_start

        if fld.id in checksum_field_ids:
            _extend_repeated(out, _ZERO_FILL, fld.width_bytes)
        elif fld.id in derived.lengths:
            _encode_scalar(out, fld, derived.lengths[fld.id])
        elif fld.has_expected_unsigned:
            _encode_scalar(out, fld, fld.expected_unsigned)
        elif fld.is_reserved:
            _extend_repeated(out, fld.reserved_fill_byte, fld.fixed_size)
        else:
            if fld.name not in values:
                raise EncodeError("missing value", fld.name)
            _encode_field(protocol, out, layout, fld, values[fld.name], values, derived)
        if track_offsets:
            field_ends[fld.id] = len(out) - layout_start

    for chk in layout.checksums:
        relative_length = len(out) - layout_start
        frm = _anchor_offset(chk.from_anchor, field_starts, field_ends, relative_length)
        to = _anchor_offset(chk.to_anchor, field_starts, field_ends, relative_length)
        try:
            digest = checksums.compute(chk.algorithm_name, memoryview(out)[layout_start + frm:layout_start + to])
        except KeyError as exc:
            raise EncodeError(f"unknown checksum algorithm '{chk.algorithm_name}'") from exc
        chk_field = layout.fields[chk.field_id]
        order = _byteorder(chk_field.byte_order)
        start = layout_start + field_starts[chk.field_id]
        out[start:start + chk_field.width_bytes] = digest.to_bytes(chk_field.width_bytes, order)


def _encode_layout(protocol: Protocol, layout: Layout, values: Mapping[str, object]) -> bytes:
    out = bytearray(layout.minimum_size)
    out.clear()
    _encode_layout_into(protocol, layout, values, out)
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


def encode(protocol: Protocol, layout: Layout, values: object) -> bytes:
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
    return _encode_layout(protocol, layout, _value_mapping(values, layout.name))


def _read_scalar(frame: memoryview, offset: int, fld: Field) -> ScalarValue:
    order = _byteorder(fld.byte_order)
    if fld.kind in ("unsigned", "enum"):
        return int.from_bytes(frame[offset:offset + fld.width_bytes], order, signed=False)
    if fld.kind == "signed":
        return int.from_bytes(frame[offset:offset + fld.width_bytes], order, signed=True)
    if fld.kind == "float32":
        return cast(float, _struct.unpack_from(_FLOAT32_FORMATS[order], frame, offset)[0])
    if fld.kind == "float64":
        return cast(float, _struct.unpack_from(_FLOAT64_FORMATS[order], frame, offset)[0])
    raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, offset)


def _decode_layout(
    protocol: Protocol,
    layout: Layout,
    frame: memoryview,
    base: int,
    bounded: bool,
    zero_copy: bool,
) -> Tuple[DecodedMapping, int]:
    """Decodes one layout from ``frame[base:]``.

    Args:
        bounded: When True the layout must consume exactly the rest of the frame
            (top-level message without trailing bytes). When False trailing
            bytes are tolerated (nested struct / sequence element).

    Returns:
        Tuple of (decoded mapping, bytes consumed).
    """
    out: DecodedMapping = {}
    offset = 0
    # Per-field offsets are only consulted when resolving checksum anchors below,
    # so avoid the dict allocations/inserts for checksum-free layouts (the common
    # case, including every element of a large collection).
    track_offsets = bool(layout.checksums)
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
            if track_offsets:
                field_starts[fld.id] = offset
                field_ends[fld.id] = offset
            continue
        offset = _align_up(offset, fld.alignment)
        if track_offsets:
            field_starts[fld.id] = offset
        offset = _decode_field(protocol, layout, fld, frame, base, offset, out, zero_copy)
        if track_offsets:
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
            digest = checksums.compute(chk.algorithm_name, frame[base + frm:base + to])
        except KeyError as exc:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, layout.fields[chk.field_id].name, chk_offset) from exc
        if int(out[layout.fields[chk.field_id].name]) != digest:
            raise DecodeError(DecodeStatus.CHECKSUM_MISMATCH, layout.fields[chk.field_id].name, chk_offset)

    return out, offset


def _decoded_present(layout: Layout, fld: Field, out: Mapping[str, object]) -> bool:
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
    frame: memoryview,
    base: int,
    offset: int,
    out: DecodedMapping,
    zero_copy: bool,
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
            try:
                out[fld.name] = _decode_text(raw, fld.string_encoding)
            except DecodeError:
                raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)
        else:
            if fld.is_reserved:
                if any(byte != fld.reserved_fill_byte for byte in raw):
                    raise DecodeError(DecodeStatus.INVALID_DATA, fld.name, base + offset)
            out[fld.name] = raw if zero_copy else raw.tobytes()
        return offset + size

    if fld.kind == "struct":
        nested = protocol.struct_by_id(fld.struct_id)
        try:
            value, consumed = _decode_layout(protocol, nested, frame, base + offset, bounded=False, zero_copy=zero_copy)
        except DecodeError as exc:
            raise DecodeError(exc.status, _prefix(fld.name, exc.field_name), exc.byte_offset) from None
        out[fld.name] = value
        return offset + consumed

    if fld.kind == "collection":
        nested = protocol.struct_by_id(fld.struct_id)
        count = int(out[layout.fields[fld.count_from_field].name]) if fld.dynamic_count else fld.fixed_count
        if nested.minimum_size > 0 and count > (available - offset) // nested.minimum_size:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
        elements: List[Dict[str, object]] = []
        for index in range(count):
            if base + offset > len(frame):
                raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, fld.name, base + offset)
            try:
                value, consumed = _decode_layout(
                    protocol, nested, frame, base + offset, bounded=False, zero_copy=zero_copy
                )
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
                    value, consumed = _decode_layout(
                        protocol, nested, frame, base + offset, bounded=False, zero_copy=zero_copy
                    )
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


def decode(protocol: Protocol, layout: Layout, frame: BytesLike, *, zero_copy: bool = False) -> DecodedMapping:
    """Decodes a single frame into a mapping of field values.

    Raises:
        DecodeError: With rich field/offset context on failure.
    """
    data = memoryview(frame)
    values, _ = _decode_layout(protocol, layout, data, 0, bounded=True, zero_copy=zero_copy)
    return values


def decode_sequence(
    protocol: Protocol, layout: Layout, frame: BytesLike, *, zero_copy: bool = False
) -> List[DecodedMapping]:
    """Decodes a packed sequence of records, tracking consumption internally.

    Most useful for struct records (length-bounded per element). Returns the
    list of decoded mappings.
    """
    data = memoryview(frame)
    records: List[DecodedMapping] = []
    offset = 0
    while offset < len(data):
        value, consumed = _decode_layout(protocol, layout, data, offset, bounded=False, zero_copy=zero_copy)
        if consumed == 0:
            raise DecodeError(DecodeStatus.SCHEMA_MISMATCH, "", offset)
        offset += consumed
        records.append(value)
    return records
