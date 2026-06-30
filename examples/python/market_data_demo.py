"""Python equivalent of ``examples/cpp/src/upr_demo.cpp``.

The C++ demo loads the ``market_data`` schema, builds three fixed-size ``Order``
frames, streams them through a transport + fixed-size framer + stream runtime,
and decodes each one.

The Python (and TypeScript) runtimes ship a pure ``Codec`` plus framing
helpers rather than the C++ ``StreamRuntime``/``SpanTransport``/``FixedSizeFramer``
trio, so this example:

* encodes ``Order`` messages with both the dict API and the generated typed
  dataclass, producing frames that are byte-for-byte identical to the C++ output;
* reconstructs the C++ "fixed-size framer" by slicing the stream into
  ``ORDER_FRAME_SIZE`` chunks and decoding each chunk.

The schema (``examples/schema/market_data.upr``) is shared with the C++ and
TypeScript examples.
"""

from __future__ import annotations

from generated import market_data
from generated.market_data import CODEC, Order

# Every Order is exactly 15 bytes on the wire (see the schema), which is what the
# C++ demo feeds to its FixedSizeFramer.
ORDER_FRAME_SIZE = market_data.PROTOCOL.message("Order").minimum_size

# The schema stores ``side``/``order_type`` as numeric enums. The wire only
# carries the integer tag, so we mirror the enum names from
# ``examples/schema/order_types.upr`` here purely for display.
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
    """Splits a byte stream into equal-size frames (the FixedSizeFramer model)."""
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

    # Frames are interchangeable with the C++ runtime: the first order encodes to
    # this exact byte sequence in every language.
    print(f"first_frame_hex={stream[:ORDER_FRAME_SIZE].hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
