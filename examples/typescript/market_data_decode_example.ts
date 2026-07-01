/**
 * Decode advanced market-data message shapes.
 *
 * Shows repeating groups, tagged variants, presence-gated optional fields, and
 * round-trip encoding from decoded values.
 */

import {
  Event,
  Quote,
  Snapshot,
  type QuoteDetail,
  type TradeDetail,
} from "./generated/advanced_market_data.ts";

function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function decodeSnapshot(): void {
  // message_type=1, level_count=2, then two Level{price, qty} records.
  const frame = Uint8Array.from([0x01, 0x02, 0x65, 0x00, 0x07, 0x00, 0x66, 0x00, 0x08, 0x00]);
  const snapshot = Snapshot.decode(frame);
  const levels = snapshot.levels!;
  if (levels.length !== snapshot.level_count) {
    throw new Error("level_count does not match decoded levels");
  }
  console.log(
    `snapshot level_count=${snapshot.level_count} ` +
      `first_price=${levels[0].price} second_qty=${levels[1].qty}`,
  );
}

function decodeQuoteEvent(): void {
  // kind=1 selects the QuoteDetail case of the variant.
  const frame = Uint8Array.from([0x02, 0x01, 99, 0x00, 103, 0x00]);
  const event = Event.decode(frame);
  const detail = event.detail as QuoteDetail;
  console.log(`event kind=${event.kind} best_bid=${detail.best_bid} best_ask=${detail.best_ask}`);
}

function decodeTradeEvent(): void {
  // kind=2 selects the TradeDetail case (a uint64 trade id, little-endian). Wide
  // integers decode to `bigint`.
  const frame = Uint8Array.from([0x02, 0x02, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00]);
  const event = Event.decode(frame);
  const detail = event.detail as TradeDetail;
  console.log(`trade_event kind=${event.kind} trade_id=${detail.trade_id}`);
}

function decodeQuoteWithNote(): void {
  // presence bit 0 set -> note_len + note are present.
  const frame = Uint8Array.from([0x03, 0x01, 0x02, "O".charCodeAt(0), "K".charCodeAt(0)]);
  const quote = Quote.decode(frame);
  console.log(`quote note=${JSON.stringify(quote.note)}`);
}

function decodeQuoteWithoutNote(): void {
  // presence bit 0 clear -> note_len + note are skipped entirely on the wire.
  const frame = Uint8Array.from([0x03, 0x00]);
  const quote = Quote.decode(frame);
  const present = quote.note !== undefined;
  console.log(`quote_without_note note_present=${present ? "yes" : "no"} presence=${quote.presence}`);
}

function main(): void {
  decodeSnapshot();
  decodeQuoteEvent();
  decodeTradeEvent();
  decodeQuoteWithNote();
  decodeQuoteWithoutNote();

  // Round-trip proof: re-encoding the decoded snapshot reproduces the frame.
  const snapshotFrame = Uint8Array.from([0x01, 0x02, 0x65, 0x00, 0x07, 0x00, 0x66, 0x00, 0x08, 0x00]);
  const reencoded = Snapshot.encode(Snapshot.decode(snapshotFrame));
  console.log(`snapshot_roundtrip_hex=${toHex(reencoded)}`);
}

main();
