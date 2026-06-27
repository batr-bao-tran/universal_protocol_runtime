"""Schema descriptor dataclasses driving the pure-Python codec.

Generated protocol modules instantiate these descriptors. The codec walks them
to encode/decode frames, so they capture every wire-relevant property of a
compiled field (alignment, presence/condition gating, collections, variants,
checksums, ...).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


@dataclass(frozen=True)
class VariantCase:
    """One tagged-variant case (tag value -> struct id)."""

    tag_value: int
    struct_id: int


@dataclass(frozen=True)
class ChecksumAnchor:
    """Resolved checksum anchor (mirrors ``CompiledChecksumAnchor``)."""

    kind: str
    field_id: int = 0


@dataclass(frozen=True)
class Checksum:
    """Compiled checksum metadata."""

    field_id: int
    result_width_bytes: int
    algorithm_name: str
    from_anchor: ChecksumAnchor
    to_anchor: ChecksumAnchor


@dataclass(frozen=True)
class Field:
    """Compiled field metadata (mirrors ``CompiledField``)."""

    id: int
    name: str
    kind: str
    width_bytes: int = 0
    byte_order: str = "little_endian"
    string_encoding: str = "ascii"
    fixed_size: int = 0
    dynamic_size: bool = False
    size_from_field: int = 0
    struct_id: int = 0
    alignment: int = 1
    is_reserved: bool = False
    reserved_fill_byte: int = 0
    fixed_count: int = 0
    dynamic_count: bool = False
    count_from_field: int = 0
    has_condition: bool = False
    condition_field: int = 0
    condition_equals: int = 0
    has_presence: bool = False
    presence_field: int = 0
    presence_bit: int = 0
    tag_from_field: int = 0
    variant_cases: Tuple[VariantCase, ...] = ()
    has_expected_unsigned: bool = False
    expected_unsigned: int = 0

    @property
    def is_gated(self) -> bool:
        return self.has_condition or self.has_presence


@dataclass(frozen=True)
class Layout:
    """Compiled message or struct layout."""

    name: str
    is_message: bool
    minimum_size: int
    allow_trailing_bytes: bool
    dispatch_prefix: bytes
    fields: Tuple[Field, ...]
    checksums: Tuple[Checksum, ...] = ()

    _fields_by_name: Dict[str, Field] = field(default_factory=dict, compare=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "_fields_by_name", {f.name: f for f in self.fields})

    def field_by_name(self, name: str) -> Field:
        return self._fields_by_name[name]


@dataclass(frozen=True)
class Protocol:
    """Compiled protocol metadata."""

    name: str
    fingerprint: int
    structs: Tuple[Layout, ...]
    messages: Tuple[Layout, ...]

    _struct_by_id: Dict[int, Layout] = field(default_factory=dict, compare=False, repr=False)
    _message_by_name: Dict[str, Layout] = field(default_factory=dict, compare=False, repr=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "_struct_by_id", {index: layout for index, layout in enumerate(self.structs)})
        object.__setattr__(self, "_message_by_name", {m.name: m for m in self.messages})

    def struct_by_id(self, struct_id: int) -> Layout:
        return self._struct_by_id[struct_id]

    def message(self, name: str) -> Optional[Layout]:
        return self._message_by_name.get(name)
