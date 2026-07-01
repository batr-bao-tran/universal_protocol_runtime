"""Encode and decode fixed-size market-data order messages.

Shows dict-based encoding, typed dataclass decoding, enum display values, and
manual splitting of a byte stream into fixed-width frames.
"""

from __future__ import annotations

from generated.market_data import CODEC, Order, PROTOCOL

# Every Order is exactly 15 bytes on the wire (see the schema).
ORDER_FRAME_SIZE = PROTOCOL.message("Order").minimum_size

# The schema stores ``side``/``order_type`` as numeric enums. The wire only
# carries the integer tag; these names come from
# ``examples/schema/order_types.upr`` and are used purely for display.
SIDE_NAMES = {1: "Buy", 2: "Sell"}
ORDER_TYPE_NAMES = {1: "Limit", 2: "Market", 3: "IOC"}

ORDERS = (
    {"symbol": "AAPL", "price": 42.25, "quantity": 100, "side": 1, "order_type": 1},
    {"symbol": "MSFT", "price": 42.50, "quantity": 25, "side": 2, "order_type": 2},
    {"symbol": "NVDA", "price": 43.00, "quantity": 10, "side": 1, "order_type": 3},
)


def build_stream() -> bytes:
    """Encodes the three orders back-to-back into one fixed-size frame stream."""
    stream = bytearray()
    for spec in ORDERS:
        # ``message_type`` carries an expected constant (= 1) and is filled in
        # automatically, so callers never set it.
        stream += CODEC.encode("Order", spec)
    return bytes(stream)


def split_fixed_frames(stream: bytes, frame_size: int) -> list[bytes]:
    """Splits a byte stream into equal-size frames."""
    if frame_size == 0 or len(stream) % frame_size != 0:
        raise ValueError("stream length is not a multiple of the frame size")
    return [stream[offset:offset + frame_size] for offset in range(0, len(stream), frame_size)]


def main() -> int:
    stream = build_stream()
    print(f"decoded_orders={len(ORDERS)}")
    print(f"stream_bytes={len(stream)} frame_size={ORDER_FRAME_SIZE}")

    for index, frame in enumerate(split_fixed_frames(stream, ORDER_FRAME_SIZE)):
        # Dict decode returns plain field values; great for generic tooling.
        values = CODEC.decode("Order", frame)
        side = SIDE_NAMES.get(int(values["side"]), str(values["side"]))
        order_type = ORDER_TYPE_NAMES.get(int(values["order_type"]), str(values["order_type"]))
        print(
            f"order[{index}] symbol={values['symbol']} type={order_type} side={side} "
            f"price={values['price']} quantity={values['quantity']}"
        )

    # The generated module also exposes a typed dataclass with ``encode``/``decode``
    # helpers for ergonomic, statically-checkable access.
    typed: Order = Order.decode(stream[:ORDER_FRAME_SIZE])
    print(f"typed_order symbol={typed.symbol} quantity={typed.quantity} price={typed.price}")

    # The first order encodes to this exact byte sequence in every runtime.
    print(f"first_frame_hex={stream[:ORDER_FRAME_SIZE].hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
