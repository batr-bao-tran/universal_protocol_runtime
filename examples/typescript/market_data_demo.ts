/**
 * TypeScript equivalent of `examples/cpp/src/upr_demo.cpp`.
 *
 * The C++ demo loads the `market_data` schema, builds three fixed-size `Order`
 * frames, and streams them through a transport + fixed-size framer + stream
 * runtime.
 *
 * The TypeScript runtime ships a dependency-free `Codec` plus framing helpers
 * rather than the C++ `StreamRuntime`/`SpanTransport`/`FixedSizeFramer`, so this
 * example encodes `Order` messages (producing byte-identical frames) and then
 * reconstructs the "fixed-size framer" by slicing the stream into
 * `ORDER_FRAME_SIZE` chunks.
 *
 * The schema (`examples/schema/market_data.upr`) is shared with the C++ and
 * Python examples.
 */

import { PROTOCOL, CODEC, Order } from "./generated/market_data.ts";

// Every Order is exactly 15 bytes on the wire, which is what the C++ demo feeds
// to its FixedSizeFramer.
const ORDER_FRAME_SIZE = PROTOCOL.messages.find((m) => m.name === "Order")!.minimumSize;

// The wire only carries the numeric enum tag; these names mirror
// `examples/schema/order_types.upr` and are used purely for display.
const SIDE_NAMES: Record<number, string> = { 1: "Buy", 2: "Sell" };
const ORDER_TYPE_NAMES: Record<number, string> = { 1: "Limit", 2: "Market", 3: "IOC" };

const ORDERS: Order[] = [
  { symbol: "AAPL", price: 42.25, quantity: 100, side: 1, order_type: 1 },
  { symbol: "MSFT", price: 42.5, quantity: 25, side: 2, order_type: 2 },
  { symbol: "NVDA", price: 43.0, quantity: 10, side: 1, order_type: 3 },
];

function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function buildStream(): Uint8Array {
  // `message_type` carries an expected constant (= 1) and is filled in
  // automatically, so callers never set it.
  const frames = ORDERS.map((order) => Order.encode(order));
  const total = frames.reduce((sum, f) => sum + f.length, 0);
  const stream = new Uint8Array(total);
  let offset = 0;
  for (const frame of frames) {
    stream.set(frame, offset);
    offset += frame.length;
  }
  return stream;
}

function splitFixedFrames(stream: Uint8Array, frameSize: number): Uint8Array[] {
  if (frameSize === 0 || stream.length % frameSize !== 0) {
    throw new Error("stream length is not a multiple of the frame size");
  }
  const frames: Uint8Array[] = [];
  for (let offset = 0; offset < stream.length; offset += frameSize) {
    frames.push(stream.subarray(offset, offset + frameSize));
  }
  return frames;
}

function main(): void {
  const stream = buildStream();
  console.log(`decoded_orders=${ORDERS.length}`);
  console.log(`stream_bytes=${stream.length} frame_size=${ORDER_FRAME_SIZE}`);

  splitFixedFrames(stream, ORDER_FRAME_SIZE).forEach((frame, index) => {
    const order = Order.decode(frame);
    const side = SIDE_NAMES[order.side!] ?? String(order.side);
    const orderType = ORDER_TYPE_NAMES[order.order_type!] ?? String(order.order_type);
    console.log(
      `order[${index}] symbol=${order.symbol} type=${orderType} side=${side} ` +
        `price=${order.price} quantity=${order.quantity}`,
    );
  });

  // Frames are interchangeable with the C++ and Python runtimes: the first order
  // encodes to this exact byte sequence in every language.
  console.log(`first_frame_hex=${toHex(stream.subarray(0, ORDER_FRAME_SIZE))}`);
}

main();
