"""Cross-language byte-compatibility and behaviour tests for the Python codec."""

import pytest

import generated_general_direct_codec as gdc
import universal_protocol_runtime.codec as raw_codec
from universal_protocol_runtime import (
    Checksum,
    ChecksumAnchor,
    Codec,
    DecodeError,
    DecodeStatus,
    EncodeError,
    Field,
    FrameDecoder,
    FramingError,
    Handshake,
    Layout,
    Protocol,
    TransportMode,
    VariantCase,
    checksums,
    check_compatibility,
    decode_handshake,
    encode_frame,
    encode_handshake,
    iter_frames,
    try_read_frame,
)

BOOK_MESSAGE_TYPE = 1
EVENT_MESSAGE_TYPE = 2
NOTE_MESSAGE_TYPE = 3
PRESENT_FLAG = 1
ABSENT_FLAG = 0
PAIR_KEYS = (7, 9)
PAIR_A_VALUES = (11, 22)
PAIR_B_VALUES = (0xDEADBEEF, 0x01020304)
BOOK_FRAME_HEX = "010207000b00efbeadde09001600040302017603"
QUOTE_KIND = 1
TRADE_KIND = 2
QUOTE_BID = 100
QUOTE_ASK = 105
TRADE_ID = 123456789012
CHECKSUM_PLACEHOLDER = 0
CHECKSUM_CORRUPTION_MASK = 0xFF
HANDSHAKE_SESSION_ID = 0x1122334455667788
INVALID_TRANSPORT_MODE = 0xFFFF
DEFAULT_PREFIX_WIDTH = 4
HANDSHAKE_PAYLOAD_SIZE = len(encode_handshake(Handshake()))
SHORT_PREFIX_WIDTH = 1
MATRIX_MAGIC = 0xA5
ALIGNED_VALUE = 0x1234
SIGNED_BIG_ENDIAN_VALUE = -2
WIDE_BIG_ENDIAN_VALUE = 0x0102030405060708
FLOAT32_VALUE = 1.5
FLOAT64_VALUE = -2.25
ASCII_LABEL = b"OK"
FIXED_BLOB = bytearray([0xAA, 0xBB])
RESERVED_FILL = 0xEE
UNKNOWN_KIND = 99
CHECKSUM_DATA = b"123456789"
XOR8_CHECK = 0x31
SUM16_CHECK = 0x01DD
CRC16_CCITT_CHECK = 0x906E
CRC32_CHECK = 0xCBF43926
CRC32C_CHECK = 0xE3069283
FRAME_ONE = b"one"
FRAME_TWO = b"two"
OVERSIZED_PAYLOAD = b"too-large"
MAX_TINY_PAYLOAD = 1


def test_builtin_checksums_match_reference_vectors() -> None:
    expected = {
        "xor8": XOR8_CHECK,
        "sum16": SUM16_CHECK,
        "crc16_ccitt": CRC16_CCITT_CHECK,
        "crc32": CRC32_CHECK,
        "crc32c": CRC32C_CHECK,
    }

    for algorithm_name, expected_value in expected.items():
        assert checksums.compute(algorithm_name, memoryview(CHECKSUM_DATA)) == expected_value


MATRIX_STRUCT = Layout(
    name="MatrixPayload",
    is_message=False,
    minimum_size=22,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(
        Field(0, "signed_be", "signed", width_bytes=2, byte_order="big_endian"),
        Field(1, "wide_be", "unsigned", width_bytes=8, byte_order="big_endian"),
        Field(2, "ratio32", "float32", width_bytes=4),
        Field(3, "ratio64", "float64", width_bytes=8, byte_order="big_endian"),
    ),
)
QUOTE_STRUCT = Layout(
    name="QuoteMini",
    is_message=False,
    minimum_size=1,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(Field(0, "bid", "unsigned", width_bytes=1),),
)
TRADE_STRUCT = Layout(
    name="TradeMini",
    is_message=False,
    minimum_size=1,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(Field(0, "size", "unsigned", width_bytes=1),),
)
COLLECTION_ITEM_STRUCT = Layout(
    name="CollectionItem",
    is_message=False,
    minimum_size=0,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(Field(0, "value", "unsigned", width_bytes=1),),
)
EMPTY_STRUCT = Layout(
    name="EmptyStruct",
    is_message=False,
    minimum_size=0,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(),
)
MATRIX_MESSAGE = Layout(
    name="Matrix",
    is_message=True,
    minimum_size=1,
    allow_trailing_bytes=False,
    dispatch_prefix=b"",
    fields=(
        Field(0, "magic", "unsigned", width_bytes=1, has_expected_unsigned=True, expected_unsigned=MATRIX_MAGIC),
        Field(1, "aligned", "unsigned", width_bytes=2, alignment=4),
        Field(2, "payload", "struct", struct_id=0),
        Field(3, "label_len", "unsigned", width_bytes=1),
        Field(4, "label", "string", dynamic_size=True, size_from_field=3),
        Field(5, "blob", "bytes", fixed_size=2),
        Field(6, "reserved", "bytes", fixed_size=2, is_reserved=True, reserved_fill_byte=RESERVED_FILL),
        Field(7, "kind", "enum", width_bytes=1),
        Field(
            8,
            "detail",
            "variant",
            tag_from_field=7,
            variant_cases=(VariantCase(QUOTE_KIND, 1), VariantCase(TRADE_KIND, 2)),
        ),
        Field(9, "crc", "unsigned", width_bytes=4),
    ),
    checksums=(
        Checksum(
            field_id=9,
            result_width_bytes=4,
            algorithm_name="crc32",
            from_anchor=ChecksumAnchor("frame_start"),
            to_anchor=ChecksumAnchor("before_self", 9),
        ),
    ),
)
MATRIX_PROTOCOL = Protocol(
    name="runtime_matrix",
    fingerprint=1,
    structs=(MATRIX_STRUCT, QUOTE_STRUCT, TRADE_STRUCT, COLLECTION_ITEM_STRUCT, EMPTY_STRUCT),
    messages=(MATRIX_MESSAGE,),
)


def matrix_value() -> dict[str, object]:
    return {
        "aligned": ALIGNED_VALUE,
        "payload": {
            "signed_be": SIGNED_BIG_ENDIAN_VALUE,
            "wide_be": WIDE_BIG_ENDIAN_VALUE,
            "ratio32": FLOAT32_VALUE,
            "ratio64": FLOAT64_VALUE,
        },
        "label": bytearray(ASCII_LABEL),
        "blob": FIXED_BLOB,
        "kind": QUOTE_KIND,
        "detail": {"bid": QUOTE_BID},
    }


def test_manual_protocol_covers_scalar_alignment_reserved_and_variant_paths() -> None:
    frame = raw_codec.encode(MATRIX_PROTOCOL, MATRIX_MESSAGE, matrix_value())

    decoded = raw_codec.decode(MATRIX_PROTOCOL, MATRIX_MESSAGE, frame)

    assert decoded["magic"] == MATRIX_MAGIC
    assert decoded["aligned"] == ALIGNED_VALUE
    assert decoded["payload"]["signed_be"] == SIGNED_BIG_ENDIAN_VALUE
    assert decoded["payload"]["wide_be"] == WIDE_BIG_ENDIAN_VALUE
    assert decoded["payload"]["ratio32"] == pytest.approx(FLOAT32_VALUE)
    assert decoded["payload"]["ratio64"] == pytest.approx(FLOAT64_VALUE)
    assert decoded["label"] == ASCII_LABEL.decode("ascii")
    assert decoded["blob"] == bytes(FIXED_BLOB)
    assert decoded["reserved"] == bytes([RESERVED_FILL, RESERVED_FILL])
    assert decoded["detail"]["bid"] == QUOTE_BID


def test_runtime_codec_reports_missing_message_by_operation() -> None:
    runtime_codec = Codec(MATRIX_PROTOCOL)

    with pytest.raises(EncodeError):
        runtime_codec.encode("Missing", {})
    with pytest.raises(DecodeError) as exc_info:
        runtime_codec.decode("Missing", b"")

    assert exc_info.value.status == DecodeStatus.MESSAGE_NOT_FOUND


def test_runtime_codec_returns_mapping_when_dataclass_is_not_registered() -> None:
    runtime_codec = Codec(MATRIX_PROTOCOL)
    frame = raw_codec.encode(MATRIX_PROTOCOL, MATRIX_MESSAGE, matrix_value())

    decoded = runtime_codec.decode_typed("Matrix", frame)

    assert decoded["aligned"] == ALIGNED_VALUE
    assert runtime_codec.protocol is MATRIX_PROTOCOL


def test_typed_decode_skips_absent_gated_fields() -> None:
    frame = gdc.CODEC.encode("Note", {"message_type": NOTE_MESSAGE_TYPE, "presence": ABSENT_FLAG})

    note = gdc.Note.decode(frame)

    assert note.presence == ABSENT_FLAG


@pytest.mark.parametrize(
    ("values", "expected_field_name"),
    [
        ({}, "aligned"),
        ({**matrix_value(), "blob": b"x"}, "blob"),
        ({**matrix_value(), "payload": {"signed_be": 1}}, "wide_be"),
        ({**matrix_value(), "kind": UNKNOWN_KIND}, "detail"),
    ],
)
def test_encode_errors_identify_fields(values: dict[str, object], expected_field_name: str) -> None:
    with pytest.raises(EncodeError) as exc_info:
        raw_codec.encode(MATRIX_PROTOCOL, MATRIX_MESSAGE, values)

    assert exc_info.value.field_name == expected_field_name


def test_encode_rejects_unknown_checksum_algorithm() -> None:
    bad_checksum_message = Layout(
        name="BadChecksum",
        is_message=True,
        minimum_size=1,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(Field(0, "value", "unsigned", width_bytes=1), Field(1, "crc", "unsigned", width_bytes=1)),
        checksums=(
            Checksum(
                field_id=1,
                result_width_bytes=1,
                algorithm_name="missing",
                from_anchor=ChecksumAnchor("frame_start"),
                to_anchor=ChecksumAnchor("before_self", 1),
            ),
        ),
    )
    protocol = Protocol(name="bad_checksum", fingerprint=2, structs=(), messages=(bad_checksum_message,))

    with pytest.raises(EncodeError):
        raw_codec.encode(protocol, bad_checksum_message, {"value": 1})


@pytest.mark.parametrize(
    ("frame", "expected_field_name", "expected_status"),
    [
        (bytes([0]), "magic", DecodeStatus.INVALID_DATA),
        (bytes([MATRIX_MAGIC, 0, 0, 0, 0, 0]), "payload", DecodeStatus.SCHEMA_MISMATCH),
    ],
)
def test_decode_errors_cover_expected_and_nested_struct_paths(
    frame: bytes, expected_field_name: str, expected_status: DecodeStatus
) -> None:
    with pytest.raises(DecodeError) as exc_info:
        raw_codec.decode(MATRIX_PROTOCOL, MATRIX_MESSAGE, frame)

    assert exc_info.value.field_name == expected_field_name
    assert exc_info.value.status == expected_status


@pytest.mark.parametrize(
    ("layout_under_test", "frame", "expected_field_name"),
    [
        (
            Layout(
                name="AsciiMessage",
                is_message=True,
                minimum_size=1,
                allow_trailing_bytes=False,
                dispatch_prefix=b"",
                fields=(
                    Field(0, "text_len", "unsigned", width_bytes=1),
                    Field(1, "text", "string", dynamic_size=True, size_from_field=0),
                ),
            ),
            bytes([1, 0xFF]),
            "text",
        ),
        (
            Layout(
                name="Utf8Message",
                is_message=True,
                minimum_size=1,
                allow_trailing_bytes=False,
                dispatch_prefix=b"",
                fields=(
                    Field(0, "text_len", "unsigned", width_bytes=1),
                    Field(1, "text", "string", string_encoding="utf8", dynamic_size=True, size_from_field=0),
                ),
            ),
            bytes([1, 0xFF]),
            "text",
        ),
        (
            Layout(
                name="ReservedMessage",
                is_message=True,
                minimum_size=2,
                allow_trailing_bytes=False,
                dispatch_prefix=b"",
                fields=(Field(0, "reserved", "bytes", fixed_size=2, is_reserved=True, reserved_fill_byte=RESERVED_FILL),),
            ),
            bytes([RESERVED_FILL, 0]),
            "reserved",
        ),
    ],
)
def test_decode_rejects_invalid_text_and_reserved_bytes(
    layout_under_test: Layout, frame: bytes, expected_field_name: str
) -> None:
    protocol = Protocol(name=layout_under_test.name, fingerprint=3, structs=(), messages=(layout_under_test,))

    with pytest.raises(DecodeError) as exc_info:
        raw_codec.decode(protocol, layout_under_test, frame)

    assert exc_info.value.field_name == expected_field_name
    assert exc_info.value.status == DecodeStatus.INVALID_DATA


def test_decode_prefixes_collection_element_errors() -> None:
    collection_message = Layout(
        name="CollectionMessage",
        is_message=True,
        minimum_size=1,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(
            Field(0, "count", "unsigned", width_bytes=1),
            Field(1, "items", "collection", struct_id=3, dynamic_count=True, count_from_field=0),
        ),
    )
    protocol = Protocol(
        name="collection_errors",
        fingerprint=4,
        structs=MATRIX_PROTOCOL.structs,
        messages=(collection_message,),
    )

    with pytest.raises(DecodeError) as exc_info:
        raw_codec.decode(protocol, collection_message, bytes([1]))

    assert exc_info.value.field_name == "items[0].value"


def test_decode_rejects_zero_width_collection_elements() -> None:
    zero_collection_message = Layout(
        name="ZeroCollection",
        is_message=True,
        minimum_size=0,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(Field(0, "items", "collection", struct_id=4, fixed_count=1),),
    )
    protocol = Protocol(
        name="zero_collection",
        fingerprint=5,
        structs=MATRIX_PROTOCOL.structs,
        messages=(zero_collection_message,),
    )

    with pytest.raises(DecodeError) as exc_info:
        raw_codec.decode(protocol, zero_collection_message, b"")

    assert exc_info.value.field_name == "items"


def test_decode_rejects_unknown_variant_and_trailing_bytes() -> None:
    valid_frame = raw_codec.encode(MATRIX_PROTOCOL, MATRIX_MESSAGE, matrix_value())
    unknown_variant_frame = bytearray(valid_frame)
    kind_offset = valid_frame.rfind(bytes([QUOTE_KIND, QUOTE_BID]))
    assert kind_offset >= 0
    unknown_variant_frame[kind_offset] = UNKNOWN_KIND

    with pytest.raises(DecodeError) as variant_error:
        raw_codec.decode(MATRIX_PROTOCOL, MATRIX_MESSAGE, bytes(unknown_variant_frame))
    with pytest.raises(DecodeError) as trailing_error:
        raw_codec.decode(MATRIX_PROTOCOL, MATRIX_MESSAGE, valid_frame + b"\x00")
    with pytest.raises(DecodeError) as bounded_struct_error:
        struct_frame = raw_codec.encode(MATRIX_PROTOCOL, MATRIX_STRUCT, matrix_value()["payload"])
        raw_codec.decode(MATRIX_PROTOCOL, MATRIX_STRUCT, struct_frame + b"\x00")

    assert variant_error.value.field_name == "detail"
    assert trailing_error.value.status == DecodeStatus.SCHEMA_MISMATCH
    assert bounded_struct_error.value.status == DecodeStatus.SCHEMA_MISMATCH


def test_codec_covers_scalar_and_unsupported_metadata_errors() -> None:
    with pytest.raises(EncodeError):
        raw_codec.encode(MATRIX_PROTOCOL, MATRIX_MESSAGE, {**matrix_value(), "aligned": 1 << 32})

    unsupported_message = Layout(
        name="Unsupported",
        is_message=True,
        minimum_size=1,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(Field(0, "mystery", "unsupported", width_bytes=1),),
    )
    protocol = Protocol(name="unsupported", fingerprint=6, structs=(), messages=(unsupported_message,))

    with pytest.raises(EncodeError):
        raw_codec.encode(protocol, unsupported_message, {"mystery": 1})
    with pytest.raises(DecodeError):
        raw_codec.decode(protocol, unsupported_message, bytes([0]))


def test_codec_covers_conditional_gates() -> None:
    conditional_message = Layout(
        name="Conditional",
        is_message=True,
        minimum_size=1,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(
            Field(0, "flag", "unsigned", width_bytes=1),
            Field(1, "value", "unsigned", width_bytes=1, has_condition=True, condition_field=0, condition_equals=1),
        ),
    )
    protocol = Protocol(name="gates", fingerprint=7, structs=(), messages=(conditional_message,))

    assert raw_codec.decode(protocol, conditional_message, raw_codec.encode(protocol, conditional_message, {"flag": 0})) == {
        "flag": 0
    }
    assert raw_codec.decode(
        protocol, conditional_message, raw_codec.encode(protocol, conditional_message, {"flag": 1, "value": 7})
    ) == {"flag": 1, "value": 7}


def test_codec_covers_variant_tag_and_nested_non_decode_failures() -> None:
    gated_tag_message = Layout(
        name="GatedTagVariant",
        is_message=True,
        minimum_size=0,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(
            Field(0, "kind", "unsigned", width_bytes=1, has_presence=True, presence_field=0, presence_bit=0),
            Field(1, "detail", "variant", tag_from_field=0, variant_cases=(VariantCase(QUOTE_KIND, 1),)),
        ),
    )
    broken_struct_message = Layout(
        name="BrokenStruct",
        is_message=True,
        minimum_size=0,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(Field(0, "missing", "struct", struct_id=99),),
    )
    protocol = Protocol(
        name="broken",
        fingerprint=8,
        structs=(QUOTE_STRUCT,),
        messages=(gated_tag_message, broken_struct_message),
    )

    with pytest.raises(EncodeError) as encode_error:
        raw_codec.encode(protocol, gated_tag_message, {"detail": {"bid": QUOTE_BID}})
    with pytest.raises(KeyError):
        raw_codec.decode(protocol, broken_struct_message, b"")

    assert encode_error.value.field_name == "kind"


def test_codec_covers_checksum_anchor_and_decode_schema_failures() -> None:
    fields = (Field(0, "value", "unsigned", width_bytes=1), Field(1, "crc", "unsigned", width_bytes=1))
    prefixed_message = Layout(
        name="Prefixed",
        is_message=True,
        minimum_size=1,
        allow_trailing_bytes=False,
        dispatch_prefix=b"\xAA",
        fields=(Field(0, "value", "unsigned", width_bytes=1),),
    )
    bad_anchor_message = Layout(
        name="BadAnchor",
        is_message=True,
        minimum_size=2,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=fields,
        checksums=(
            Checksum(1, 1, "xor8", ChecksumAnchor("not_real", 0), ChecksumAnchor("before_self", 1)),
        ),
    )
    reversed_anchor_message = Layout(
        name="ReversedAnchor",
        is_message=True,
        minimum_size=2,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=fields,
        checksums=(
            Checksum(1, 1, "xor8", ChecksumAnchor("frame_end", 0), ChecksumAnchor("field_start", 0)),
        ),
    )
    unknown_checksum_message = Layout(
        name="UnknownChecksum",
        is_message=True,
        minimum_size=2,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=fields,
        checksums=(
            Checksum(1, 1, "missing", ChecksumAnchor("frame_start", 0), ChecksumAnchor("before_self", 1)),
        ),
    )
    protocol = Protocol(
        name="checksum_errors",
        fingerprint=9,
        structs=(),
        messages=(prefixed_message, bad_anchor_message, reversed_anchor_message, unknown_checksum_message),
    )

    with pytest.raises(DecodeError):
        raw_codec.decode(protocol, prefixed_message, b"\xBB")
    with pytest.raises(EncodeError):
        raw_codec.encode(protocol, bad_anchor_message, {"value": 1})
    for layout_under_test in (bad_anchor_message, reversed_anchor_message, unknown_checksum_message):
        with pytest.raises(DecodeError):
            raw_codec.decode(protocol, layout_under_test, bytes([1, 0]))


def test_codec_covers_field_end_checksum_anchors_and_fixed_size_truncation() -> None:
    anchored_checksum_message = Layout(
        name="AnchoredChecksum",
        is_message=True,
        minimum_size=3,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(
            Field(0, "value", "unsigned", width_bytes=1),
            Field(1, "crc", "unsigned", width_bytes=2, byte_order="big_endian"),
        ),
        checksums=(
            Checksum(1, 2, "sum16", ChecksumAnchor("frame_start", 0), ChecksumAnchor("field_end", 0)),
        ),
    )
    fixed_bytes_message = Layout(
        name="FixedBytes",
        is_message=True,
        minimum_size=2,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(Field(0, "blob", "bytes", fixed_size=2),),
    )
    protocol = Protocol(
        name="codec_edges",
        fingerprint=11,
        structs=(),
        messages=(anchored_checksum_message, fixed_bytes_message),
    )

    assert raw_codec.encode(protocol, anchored_checksum_message, {"value": 1}) == bytes([1, 0, 1])
    with pytest.raises(DecodeError):
        raw_codec.decode(protocol, fixed_bytes_message, bytes([1]))


def test_decode_sequence_rejects_zero_width_records() -> None:
    empty_struct = Layout(
        name="EmptyRecord",
        is_message=False,
        minimum_size=0,
        allow_trailing_bytes=False,
        dispatch_prefix=b"",
        fields=(),
    )
    protocol = Protocol(name="empty", fingerprint=10, structs=(empty_struct,), messages=())

    with pytest.raises(DecodeError):
        raw_codec.decode_sequence(protocol, empty_struct, bytes([0]))


def book_value(pair_b_values: tuple[int, int] = PAIR_B_VALUES) -> dict[str, object]:
    pairs = [
        {"key": key, "value": {"a": a_value, "b": b_value}}
        for key, a_value, b_value in zip(PAIR_KEYS, PAIR_A_VALUES, pair_b_values)
    ]
    return {
        "message_type": BOOK_MESSAGE_TYPE,
        "count": len(pairs),
        "pairs": pairs,
        "checksum": CHECKSUM_PLACEHOLDER,
    }


def single_pair_book_value(pair_b: int = 1) -> dict[str, object]:
    return {
        "message_type": BOOK_MESSAGE_TYPE,
        "count": 1,
        "pairs": [{"key": PAIR_KEYS[0], "value": {"a": PAIR_A_VALUES[0], "b": pair_b}}],
        "checksum": CHECKSUM_PLACEHOLDER,
    }


def test_book_frame_matches_cpp_wire_format() -> None:
    frame = gdc.CODEC.encode("Book", book_value())

    assert frame == bytes.fromhex(BOOK_FRAME_HEX)

    decoded = gdc.CODEC.decode("Book", frame)
    assert decoded["count"] == len(PAIR_KEYS)
    assert decoded["pairs"][1]["value"]["b"] == PAIR_B_VALUES[1]


@pytest.mark.parametrize(
    ("values", "expected_frame", "expected_note"),
    [
        (
            {
                "message_type": NOTE_MESSAGE_TYPE,
                "presence": PRESENT_FLAG,
                "note_len": len("hello"),
                "note": "hello",
            },
            bytes([NOTE_MESSAGE_TYPE, PRESENT_FLAG, len("hello")]) + b"hello",
            "hello",
        ),
        (
            {"message_type": NOTE_MESSAGE_TYPE, "presence": ABSENT_FLAG},
            bytes([NOTE_MESSAGE_TYPE, ABSENT_FLAG]),
            None,
        ),
    ],
)
def test_note_presence_gated_vectors(
    values: dict[str, object], expected_frame: bytes, expected_note: str | None
) -> None:
    frame = gdc.CODEC.encode("Note", values)

    assert frame == expected_frame
    decoded = gdc.CODEC.decode("Note", frame)
    if expected_note is None:
        assert "note" not in decoded
    else:
        assert decoded["note"] == expected_note


def test_variant_round_trip() -> None:
    frame = gdc.CODEC.encode(
        "Event",
        {"message_type": EVENT_MESSAGE_TYPE, "kind": QUOTE_KIND, "detail": {"bid": QUOTE_BID, "ask": QUOTE_ASK}},
    )

    decoded = gdc.CODEC.decode("Event", frame)

    assert decoded["detail"]["bid"] == QUOTE_BID
    assert decoded["detail"]["ask"] == QUOTE_ASK


def test_auto_derives_length_and_count_fields() -> None:
    note = "hi"
    frame = gdc.CODEC.encode(
        "Note", {"message_type": NOTE_MESSAGE_TYPE, "presence": PRESENT_FLAG, "note_len": 0, "note": note}
    )

    assert frame == bytes([NOTE_MESSAGE_TYPE, PRESENT_FLAG, len(note)]) + note.encode()


@pytest.mark.parametrize(
    ("mutate_frame", "expected_status", "expected_field_name"),
    [
        (lambda frame: frame[:-1], DecodeStatus.SCHEMA_MISMATCH, "checksum"),
        (
            lambda frame: bytes(frame[:-1] + bytes([frame[-1] ^ CHECKSUM_CORRUPTION_MASK])),
            DecodeStatus.CHECKSUM_MISMATCH,
            "checksum",
        ),
    ],
)
def test_decode_errors_report_field_and_status(mutate_frame, expected_status: DecodeStatus, expected_field_name: str) -> None:
    frame = gdc.CODEC.encode("Book", single_pair_book_value())

    with pytest.raises(DecodeError) as exc_info:
        gdc.CODEC.decode("Book", mutate_frame(frame))

    assert exc_info.value.status == expected_status
    assert exc_info.value.field_name == expected_field_name


def test_collection_error_path() -> None:
    truncated_pair_prefix = bytes([BOOK_MESSAGE_TYPE, 1, PAIR_KEYS[0], 0])

    with pytest.raises(DecodeError) as exc_info:
        gdc.CODEC.decode("Book", truncated_pair_prefix)

    assert exc_info.value.field_name == "pairs"


@pytest.mark.parametrize("pair_values", [(1, 2), (9, 10)])
def test_decode_sequence(pair_values: tuple[int, int]) -> None:
    records_in = [
        {"key": PAIR_KEYS[0], "value": {"a": PAIR_A_VALUES[0], "b": pair_values[0]}},
        {"key": PAIR_KEYS[1], "value": {"a": PAIR_A_VALUES[1], "b": pair_values[1]}},
    ]
    blob = b"".join(gdc.CODEC.encode("Pair", record) for record in records_in)

    records_out = gdc.CODEC.decode_sequence("Pair", blob)

    assert [record["key"] for record in records_out] == list(PAIR_KEYS)
    assert [record["value"]["b"] for record in records_out] == list(pair_values)


def test_decode_sequence_partial_tail() -> None:
    blob = gdc.CODEC.encode(
        "Pair", {"key": PAIR_KEYS[0], "value": {"a": PAIR_A_VALUES[0], "b": PAIR_B_VALUES[0]}}
    )

    with pytest.raises(DecodeError):
        gdc.CODEC.decode_sequence("Pair", blob[:-1])


def test_dataclass_round_trip() -> None:
    frame = gdc.CODEC.encode("Book", single_pair_book_value(pair_b=PAIR_B_VALUES[0]))

    book = gdc.Book.decode(frame)

    assert book.pairs[0].value.b == PAIR_B_VALUES[0]
    assert book.encode() == frame


def test_variant_decodes_to_registered_dataclass() -> None:
    frame = gdc.CODEC.encode(
        "Event", {"message_type": EVENT_MESSAGE_TYPE, "kind": TRADE_KIND, "detail": {"trade_id": TRADE_ID}}
    )

    event = gdc.Event.decode(frame)

    assert isinstance(event.detail, gdc.TradeDetail)
    assert event.detail.trade_id == TRADE_ID


@pytest.mark.parametrize("payload", [b"hello world", bytes(range(DEFAULT_PREFIX_WIDTH))])
def test_length_prefixed_frame_round_trip(payload: bytes) -> None:
    wire = encode_frame(payload)

    assert wire == len(payload).to_bytes(DEFAULT_PREFIX_WIDTH, "little") + payload
    assert try_read_frame(wire) == (payload, len(wire))


def test_iter_frames_yields_complete_prefix_width_frames() -> None:
    wire = encode_frame(FRAME_ONE, prefix_width=SHORT_PREFIX_WIDTH) + encode_frame(
        FRAME_TWO, prefix_width=SHORT_PREFIX_WIDTH
    )

    assert list(iter_frames(wire, prefix_width=SHORT_PREFIX_WIDTH)) == [FRAME_ONE, FRAME_TWO]


@pytest.mark.parametrize(
    "operation",
    [
        lambda: encode_frame(b"", prefix_width=3),
        lambda: try_read_frame(b"", prefix_width=3),
        lambda: FrameDecoder(prefix_width=3),
        lambda: encode_frame(OVERSIZED_PAYLOAD, max_payload=MAX_TINY_PAYLOAD),
        lambda: try_read_frame(encode_frame(OVERSIZED_PAYLOAD), max_payload=MAX_TINY_PAYLOAD),
    ],
)
def test_framing_rejects_invalid_options_and_oversized_payloads(operation) -> None:
    with pytest.raises(FramingError):
        operation()


def test_try_read_frame_reports_partial_inputs() -> None:
    wire = encode_frame(FRAME_ONE, prefix_width=SHORT_PREFIX_WIDTH)

    assert try_read_frame(wire[:0], prefix_width=SHORT_PREFIX_WIDTH) is None
    assert try_read_frame(wire[:-1], prefix_width=SHORT_PREFIX_WIDTH) is None


def test_frame_decoder_handles_partial_feeds() -> None:
    payload = b"hello world"
    wire = encode_frame(payload)
    split_at = DEFAULT_PREFIX_WIDTH - 1
    decoder = FrameDecoder()

    assert decoder.feed(wire[:split_at]) == []
    assert decoder.feed(wire[split_at:]) == [payload]


def test_frame_decoder_rejects_oversized_and_retains_partial_payloads() -> None:
    oversized_decoder = FrameDecoder(max_payload=MAX_TINY_PAYLOAD)
    partial_decoder = FrameDecoder()
    wire = encode_frame(FRAME_TWO)

    with pytest.raises(FramingError):
        oversized_decoder.feed(encode_frame(OVERSIZED_PAYLOAD))
    assert partial_decoder.feed(wire[:-1]) == []


def test_handshake_matches_cpp_layout() -> None:
    handshake = Handshake(session_id=HANDSHAKE_SESSION_ID)
    encoded = encode_handshake(handshake)

    round_trip = decode_handshake(encoded)

    assert round_trip == handshake
    check_compatibility(handshake, round_trip)


def test_handshake_rejects_invalid_transport_mode() -> None:
    encoded = bytearray(encode_handshake(Handshake()))
    encoded[8:10] = INVALID_TRANSPORT_MODE.to_bytes(2, "little")

    with pytest.raises(FramingError):
        decode_handshake(bytes(encoded))


@pytest.mark.parametrize(
    "payload",
    [
        b"",
        b"BPR1" + bytes(HANDSHAKE_PAYLOAD_SIZE - len("UPR1")),
    ],
)
def test_handshake_rejects_malformed_payloads(payload: bytes) -> None:
    with pytest.raises(FramingError):
        decode_handshake(payload)


@pytest.mark.parametrize(
    "remote",
    [
        Handshake(protocol_version=2),
        Handshake(transport_mode=TransportMode.DATAGRAM),
        Handshake(frame_codec=2),
        Handshake(max_frame_bytes=MAX_TINY_PAYLOAD),
    ],
)
def test_handshake_rejects_incompatible_peers(remote: Handshake) -> None:
    with pytest.raises(FramingError):
        check_compatibility(Handshake(), remote)


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
