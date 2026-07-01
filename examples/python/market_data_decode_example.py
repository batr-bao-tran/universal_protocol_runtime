"""Decode advanced market-data message shapes.

Shows repeating groups, tagged variants, presence-gated optional fields, typed
dataclass decoding, and round-trip encoding from decoded values.
"""

from __future__ import annotations

from generated.advanced_market_data import CODEC, Event, Quote, Snapshot


def decode_snapshot() -> None:
    # message_type=1, level_count=2, then two Level{price, qty} records.
    frame = bytes([0x01, 0x02, 0x65, 0x00, 0x07, 0x00, 0x66, 0x00, 0x08, 0x00])
    snapshot = CODEC.decode("Snapshot", frame)
    levels = snapshot["levels"]
    assert len(levels) == snapshot["level_count"] == 2
    print(
        f"snapshot level_count={snapshot['level_count']} "
        f"first_price={levels[0]['price']} second_qty={levels[1]['qty']}"
    )

    # Typed access reconstructs the nested Level dataclasses.
    typed: Snapshot = Snapshot.decode(frame)
    print(f"  typed levels={[ (lvl.price, lvl.qty) for lvl in typed.levels ]}")


def decode_quote_event() -> None:
    # kind=1 selects the QuoteDetail case of the variant.
    frame = bytes([0x02, 0x01, 99, 0x00, 103, 0x00])
    event = CODEC.decode("Event", frame)
    detail = event["detail"]
    print(f"event kind={event['kind']} best_bid={detail['best_bid']} best_ask={detail['best_ask']}")


def decode_trade_event() -> None:
    # kind=2 selects the TradeDetail case (a uint64 trade id, little-endian).
    frame = bytes([0x02, 0x02, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00])
    event: Event = Event.decode(frame)
    # The variant decodes to whichever case the tag selected.
    print(f"trade_event kind={event.kind} trade_id={event.detail.trade_id}")


def decode_quote_with_note() -> None:
    # presence bit 0 set -> note_len + note are present.
    frame = bytes([0x03, 0x01, 0x02, ord("O"), ord("K")])
    quote = CODEC.decode("Quote", frame)
    print(f"quote note={quote['note']!r}")


def decode_quote_without_note() -> None:
    # presence bit 0 clear -> note_len + note are skipped entirely on the wire.
    frame = bytes([0x03, 0x00])
    quote: Quote = Quote.decode(frame)
    present = "note" in CODEC.decode("Quote", frame)
    print(f"quote_without_note note_present={'yes' if present else 'no'} presence={quote.presence}")


def main() -> int:
    decode_snapshot()
    decode_quote_event()
    decode_trade_event()
    decode_quote_with_note()
    decode_quote_without_note()
    # Round-trip proof: re-encoding the decoded snapshot reproduces the frame.
    snapshot = CODEC.decode("Snapshot", bytes([0x01, 0x02, 0x65, 0x00, 0x07, 0x00, 0x66, 0x00, 0x08, 0x00]))
    reencoded = CODEC.encode("Snapshot", snapshot)
    print(f"snapshot_roundtrip_hex={reencoded.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
