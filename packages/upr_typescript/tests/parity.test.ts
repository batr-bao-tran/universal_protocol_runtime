import assert from "node:assert/strict";
import { test } from "node:test";

import { EncodeError } from "../dist/index.js";
import { Book, Event, Note } from "./generated_general_direct_codec.ts";

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

// Reference frames produced by the Python runtime (which is verified
// byte-compatible with the C++ direct codec) for the shared
// `general_direct_codec.upr` schema. The TypeScript runtime must reproduce
// these exact bytes.
const REFERENCE = {
  book: "0102070001000200000009000300040000001d00",
  canonicalBook: "010207000b00efbeadde09001600040302017603",
  eventQuote: "020164000000c8000000",
  eventTrade: "0202141a99be1c000000",
  notePresent: "03010568656c6c6f",
  noteAbsent: "0300",
};

test("Book encodes byte-identically to the Python/C++ runtime", () => {
  const frame = Book.encode({
    count: 2,
    pairs: [
      { key: 7, value: { a: 1, b: 2 } },
      { key: 9, value: { a: 3, b: 4 } },
    ],
  });
  assert.equal(hex(frame), REFERENCE.book);
  const decoded = Book.decode(frame);
  assert.equal(decoded.count, 2);
  assert.deepEqual(decoded.pairs, [
    { key: 7, value: { a: 1, b: 2 } },
    { key: 9, value: { a: 3, b: 4 } },
  ]);
});

test("Book matches the canonical C++/Python checksum vector", () => {
  const frame = Book.encode({
    count: 2,
    pairs: [
      { key: 7, value: { a: 11, b: 0xdeadbeef } },
      { key: 9, value: { a: 22, b: 0x01020304 } },
    ],
  });
  assert.equal(hex(frame), REFERENCE.canonicalBook);
});

test("Event (quote variant) is byte-identical", () => {
  const frame = Event.encode({ kind: 1, detail: { bid: 100, ask: 200 } });
  assert.equal(hex(frame), REFERENCE.eventQuote);
  const decoded = Event.decode(frame);
  assert.equal(decoded.kind, 1);
  assert.deepEqual(decoded.detail, { bid: 100, ask: 200 });
});

test("Event (trade variant) round-trips a 64-bit id as bigint", () => {
  const frame = Event.encode({ kind: 2, detail: { trade_id: 123456789012 } });
  assert.equal(hex(frame), REFERENCE.eventTrade);
  const decoded = Event.decode(frame);
  assert.equal(decoded.kind, 2);
  assert.deepEqual(decoded.detail, { trade_id: 123456789012n });
});

test("Event rejects unsafe 64-bit number input", () => {
  assert.throws(
    () => Event.encode({ kind: 2, detail: { trade_id: Number.MAX_SAFE_INTEGER + 2 } }),
    EncodeError,
  );
});

test("Note honours presence gating (present)", () => {
  const frame = Note.encode({ presence: 1, note: "hello" });
  assert.equal(hex(frame), REFERENCE.notePresent);
  const decoded = Note.decode(frame);
  assert.equal(decoded.note, "hello");
  assert.equal(decoded.note_len, 5);
});

test("Note honours presence gating (absent)", () => {
  const frame = Note.encode({ presence: 0 });
  assert.equal(hex(frame), REFERENCE.noteAbsent);
  const decoded = Note.decode(frame);
  assert.equal(decoded.presence, 0);
  assert.equal(decoded.note, undefined);
});
