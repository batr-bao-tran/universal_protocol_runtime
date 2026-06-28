"""High-level codec facade for runtime schemas and generated Python modules."""

from __future__ import annotations

import dataclasses
from typing import Callable, Dict, List, Mapping

from . import codec as _codec
from .errors import DecodeError, DecodeStatus, EncodeError
from .metadata import Layout, Protocol

_DataclassFactory = Callable[..., object]


class Codec:
    """Encodes/decodes any message in a protocol by name.

    Runtime-provided schemas use the pure Python codec underneath. Generated
    protocol modules subclass this facade and delegate encode/decode to their
    native extension while reusing typed dataclass reconstruction.
    """

    def __init__(self, protocol: Protocol) -> None:
        self._protocol = protocol
        self._dataclass_by_layout: Dict[str, _DataclassFactory] = {}

    @property
    def protocol(self) -> Protocol:
        return self._protocol

    def register_dataclass(self, layout_name: str, cls: _DataclassFactory) -> None:
        """Associates a dataclass with a layout name for typed decoding."""
        self._dataclass_by_layout[layout_name] = cls

    def _layout(self, name: str, *, for_encode: bool = False) -> Layout:
        layout = self._protocol.message(name)
        if layout is None:
            for struct in self._protocol.structs:
                if struct.name == name:
                    return struct
            if for_encode:
                raise EncodeError("message not found", name)
            raise DecodeError(DecodeStatus.MESSAGE_NOT_FOUND, "", 0)
        return layout

    def encode(self, name: str, values: object) -> bytes:
        """Encodes a mapping (or dataclass) into a frame."""
        if dataclasses.is_dataclass(values) and not isinstance(values, type):
            values = {field.name: getattr(values, field.name) for field in dataclasses.fields(values)}
        if not isinstance(values, Mapping):
            raise EncodeError("expected mapping or dataclass", name)
        return _codec.encode(self._protocol, self._layout(name, for_encode=True), values)

    def decode(self, name: str, frame: _codec.BytesLike, *, zero_copy: bool = False) -> _codec.DecodedMapping:
        """Decodes one frame into a mapping of field values."""
        return _codec.decode(self._protocol, self._layout(name), frame, zero_copy=zero_copy)

    def decode_sequence(
        self, name: str, frame: _codec.BytesLike, *, zero_copy: bool = False
    ) -> List[_codec.DecodedMapping]:
        """Decodes a packed sequence of records into a list of mappings."""
        return _codec.decode_sequence(self._protocol, self._layout(name), frame, zero_copy=zero_copy)

    def decode_typed(self, name: str, frame: _codec.BytesLike) -> object:
        """Decodes one frame into the registered dataclass instance."""
        mapping = self.decode(name, frame)
        return self._to_instance(self._layout(name), mapping)

    def _to_instance(self, layout: Layout, mapping: Mapping[str, object]) -> object:
        cls = self._dataclass_by_layout.get(layout.name)
        if cls is None:
            return dict(mapping)
        kwargs: Dict[str, object] = {}
        for fld in layout.fields:
            if fld.name not in mapping:
                continue
            value = mapping[fld.name]
            if fld.kind == "struct" and isinstance(value, Mapping):
                value = self._to_instance(self._protocol.struct_by_id(fld.struct_id), value)
            elif fld.kind == "collection" and isinstance(value, list):
                nested = self._protocol.struct_by_id(fld.struct_id)
                value = [self._to_instance(nested, item) for item in value]
            elif fld.kind == "variant" and isinstance(value, Mapping):
                tag_value = int(mapping[self._field_name(layout, fld.tag_from_field)])
                for case in fld.variant_cases:
                    if case.tag_value == tag_value:
                        value = self._to_instance(self._protocol.struct_by_id(case.struct_id), value)
                        break
            kwargs[fld.name] = value
        return cls(**kwargs)

    def _field_name(self, layout: Layout, field_id: int) -> str:
        return layout.fields[field_id].name
