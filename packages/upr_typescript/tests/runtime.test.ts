import assert from "node:assert/strict";
import { test } from "node:test";

import { Codec, DecodeError, EncodeError, checksums, framing, metadata } from "../dist/index.js";
import type { Field, Layout, Protocol } from "../dist/index.js";

function field(overrides: Partial<Field> & Pick<Field, "id" | "name" | "kind">): Field {
  return {
    widthBytes: 0,
    byteOrder: "little_endian",
    stringEncoding: "ascii",
    fixedSize: 0,
    dynamicSize: false,
    sizeFromField: 0,
    structId: 0,
    alignment: 1,
    isReserved: false,
    reservedFillByte: 0,
    fixedCount: 0,
    dynamicCount: false,
    countFromField: 0,
    hasCondition: false,
    conditionField: 0,
    conditionEquals: 0,
    hasPresence: false,
    presenceField: 0,
    presenceBit: 0,
    tagFromField: 0,
    variantCases: [],
    hasExpectedUnsigned: false,
    expectedUnsigned: 0,
    ...overrides,
  };
}

function layout(overrides: Partial<Layout> & Pick<Layout, "name" | "fields">): Layout {
  return {
    isMessage: false,
    minimumSize: 0,
    allowTrailingBytes: false,
    dispatchPrefix: new Uint8Array(0),
    checksums: [],
    ...overrides,
  };
}

// Pair { key: u16 LE, value: u16 LE }  (struct id 0)
const pairStruct = layout({
  name: "Pair",
  minimumSize: 4,
  fields: [
    field({ id: 0, name: "key", kind: "unsigned", widthBytes: 2 }),
    field({ id: 1, name: "value", kind: "unsigned", widthBytes: 2 }),
  ],
});

// Message Bag { count: u8, pairs: collection<Pair> count_from count, crc: crc16 }
const bagMessage = layout({
  name: "Bag",
  isMessage: true,
  minimumSize: 3,
  fields: [
    field({ id: 0, name: "count", kind: "unsigned", widthBytes: 1 }),
    field({
      id: 1,
      name: "pairs",
      kind: "collection",
      structId: 0,
      dynamicCount: true,
      countFromField: 0,
    }),
    field({ id: 2, name: "crc", kind: "unsigned", widthBytes: 2, byteOrder: "little_endian" }),
  ],
  checksums: [
    {
      fieldId: 2,
      resultWidthBytes: 2,
      algorithmName: "crc16_ccitt",
      fromAnchor: { kind: "frame_start", fieldId: 0 },
      toAnchor: { kind: "before_self", fieldId: 2 },
    },
  ],
});

const protocol: Protocol = {
  name: "demo",
  fingerprint: "0",
  structs: [pairStruct],
  messages: [bagMessage],
};

const codec = new Codec(protocol);
const matrixMagic = 0xa5;
const alignedValue = 0x1234;
const signedBigEndianValue = -2;
const wideBigEndianValue = 0x0102030405060708n;
const float32Value = 1.5;
const float64Value = -2.25;
const asciiLabel = new Uint8Array([0x4f, 0x4b]);
const fixedBlob = [0xaa, 0xbb];
const reservedFill = 0xee;
const quoteKind = 1;
const tradeKind = 2;
const quoteBid = 100;
const unknownKind = 99;
const checksumData = new TextEncoder().encode("123456789");
const xor8Check = 0x31;
const sum16Check = 0x01dd;
const crc16CcittCheck = 0x906e;
const crc32Check = 0xcbf43926;
const crc32cCheck = 0xe3069283;
const frameOne = new Uint8Array([0x6f, 0x6e, 0x65]);
const frameTwo = new Uint8Array([0x74, 0x77, 0x6f]);
const maxTinyPayload = 1;
const invalidPrefixWidth = 3;

const matrixStruct = layout({
  name: "MatrixPayload",
  minimumSize: 22,
  fields: [
    field({ id: 0, name: "signedBe", kind: "signed", widthBytes: 2, byteOrder: "big_endian" }),
    field({ id: 1, name: "wideBe", kind: "unsigned", widthBytes: 8, byteOrder: "big_endian" }),
    field({ id: 2, name: "ratio32", kind: "float32", widthBytes: 4 }),
    field({ id: 3, name: "ratio64", kind: "float64", widthBytes: 8, byteOrder: "big_endian" }),
  ],
});
const quoteStruct = layout({
  name: "QuoteMini",
  minimumSize: 1,
  fields: [field({ id: 0, name: "bid", kind: "unsigned", widthBytes: 1 })],
});
const tradeStruct = layout({
  name: "TradeMini",
  minimumSize: 1,
  fields: [field({ id: 0, name: "size", kind: "unsigned", widthBytes: 1 })],
});
const collectionItemStruct = layout({
  name: "CollectionItem",
  minimumSize: 0,
  fields: [field({ id: 0, name: "value", kind: "unsigned", widthBytes: 1 })],
});
const emptyStruct = layout({ name: "EmptyStruct", minimumSize: 0, fields: [] });
const matrixMessage = layout({
  name: "Matrix",
  isMessage: true,
  minimumSize: 1,
  fields: [
    field({
      id: 0,
      name: "magic",
      kind: "unsigned",
      widthBytes: 1,
      hasExpectedUnsigned: true,
      expectedUnsigned: matrixMagic,
    }),
    field({ id: 1, name: "aligned", kind: "unsigned", widthBytes: 2, alignment: 4 }),
    field({ id: 2, name: "payload", kind: "struct", structId: 0 }),
    field({ id: 3, name: "labelLen", kind: "unsigned", widthBytes: 1 }),
    field({ id: 4, name: "label", kind: "string", dynamicSize: true, sizeFromField: 3 }),
    field({ id: 5, name: "blob", kind: "bytes", fixedSize: 2 }),
    field({ id: 6, name: "reserved", kind: "bytes", fixedSize: 2, isReserved: true, reservedFillByte: reservedFill }),
    field({ id: 7, name: "kind", kind: "enum", widthBytes: 1 }),
    field({
      id: 8,
      name: "detail",
      kind: "variant",
      tagFromField: 7,
      variantCases: [
        { tagValue: quoteKind, structId: 1 },
        { tagValue: tradeKind, structId: 2 },
      ],
    }),
    field({ id: 9, name: "crc", kind: "unsigned", widthBytes: 4 }),
  ],
  checksums: [
    {
      fieldId: 9,
      resultWidthBytes: 4,
      algorithmName: "crc32",
      fromAnchor: { kind: "frame_start", fieldId: 0 },
      toAnchor: { kind: "before_self", fieldId: 9 },
    },
  ],
});
const matrixProtocol: Protocol = {
  name: "runtime_matrix",
  fingerprint: "1",
  structs: [matrixStruct, quoteStruct, tradeStruct, collectionItemStruct, emptyStruct],
  messages: [matrixMessage],
};
const matrixCodec = new Codec(matrixProtocol);

function matrixValue(): Record<string, unknown> {
  return {
    aligned: alignedValue,
    payload: {
      signedBe: signedBigEndianValue,
      wideBe: wideBigEndianValue,
      ratio32: float32Value,
      ratio64: float64Value,
    },
    label: asciiLabel,
    blob: fixedBlob,
    kind: quoteKind,
    detail: { bid: quoteBid },
  };
}

test("built-in checksums match reference vectors", () => {
  assert.equal(checksums.xor8(checksumData), xor8Check);
  assert.equal(checksums.sum16(checksumData), sum16Check);
  assert.equal(checksums.crc16Ccitt(checksumData), crc16CcittCheck);
  assert.equal(checksums.crc32(checksumData) >>> 0, crc32Check);
  assert.equal(checksums.crc32c(checksumData) >>> 0, crc32cCheck);
});

test("manual protocol covers scalar alignment reserved and variant paths", () => {
  const frame = matrixCodec.encode("Matrix", matrixValue());
  const decoded = matrixCodec.decode("Matrix", frame);

  assert.equal(decoded.magic, matrixMagic);
  assert.equal(decoded.aligned, alignedValue);
  assert.equal((decoded.payload as Record<string, unknown>).signedBe, signedBigEndianValue);
  assert.equal((decoded.payload as Record<string, unknown>).wideBe, wideBigEndianValue);
  assert.equal((decoded.payload as Record<string, unknown>).ratio32, float32Value);
  assert.equal((decoded.payload as Record<string, unknown>).ratio64, float64Value);
  assert.equal(decoded.label, new TextDecoder("ascii").decode(asciiLabel));
  assert.deepEqual(decoded.blob, Uint8Array.from(fixedBlob));
  assert.deepEqual(decoded.reserved, new Uint8Array([reservedFill, reservedFill]));
  assert.deepEqual(decoded.detail, { bid: quoteBid });
});

test("runtime codec reports missing message by operation", () => {
  assert.throws(() => matrixCodec.encode("Missing", {}), EncodeError);
  assert.throws(
    () => matrixCodec.decode("Missing", new Uint8Array(0)),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.status, "message_not_found");
      return true;
    },
  );
});

test("encode errors identify fields", () => {
  const cases: Array<{ values: Record<string, unknown>; fieldName: string }> = [
    { values: {}, fieldName: "aligned" },
    { values: { ...matrixValue(), blob: [0xaa] }, fieldName: "blob" },
    { values: { ...matrixValue(), payload: { signedBe: signedBigEndianValue } }, fieldName: "wideBe" },
    { values: { ...matrixValue(), kind: unknownKind }, fieldName: "detail" },
  ];

  for (const { values, fieldName } of cases) {
    assert.throws(
      () => matrixCodec.encode("Matrix", values),
      (error: unknown) => {
        assert.ok(error instanceof EncodeError);
        assert.equal(error.fieldName, fieldName);
        return true;
      },
    );
  }
});

test("encode rejects unknown checksum algorithms", () => {
  const badChecksumMessage = layout({
    name: "BadChecksum",
    isMessage: true,
    minimumSize: 1,
    fields: [
      field({ id: 0, name: "value", kind: "unsigned", widthBytes: 1 }),
      field({ id: 1, name: "crc", kind: "unsigned", widthBytes: 1 }),
    ],
    checksums: [
      {
        fieldId: 1,
        resultWidthBytes: 1,
        algorithmName: "missing",
        fromAnchor: { kind: "frame_start", fieldId: 0 },
        toAnchor: { kind: "before_self", fieldId: 1 },
      },
    ],
  });
  const badChecksumCodec = new Codec({
    name: "bad_checksum",
    fingerprint: "2",
    structs: [],
    messages: [badChecksumMessage],
  });

  assert.throws(() => badChecksumCodec.encode("BadChecksum", { value: 1 }), EncodeError);
});

test("decode errors cover expected constants and nested structs", () => {
  const cases: Array<{ frame: Uint8Array; fieldName: string; status: string }> = [
    { frame: new Uint8Array([0]), fieldName: "magic", status: "invalid_data" },
    { frame: new Uint8Array([matrixMagic, 0, 0, 0, 0, 0]), fieldName: "payload", status: "schema_mismatch" },
  ];

  for (const { frame, fieldName, status } of cases) {
    assert.throws(
      () => matrixCodec.decode("Matrix", frame),
      (error: unknown) => {
        assert.ok(error instanceof DecodeError);
        assert.equal(error.fieldName, fieldName);
        assert.equal(error.status, status);
        return true;
      },
    );
  }
});

test("decode rejects invalid text and reserved bytes", () => {
  const cases: Array<{ layoutUnderTest: Layout; frame: Uint8Array; fieldName: string }> = [
    {
      layoutUnderTest: layout({
        name: "AsciiMessage",
        isMessage: true,
        minimumSize: 1,
        fields: [
          field({ id: 0, name: "textLen", kind: "unsigned", widthBytes: 1 }),
          field({ id: 1, name: "text", kind: "string", dynamicSize: true, sizeFromField: 0 }),
        ],
      }),
      frame: new Uint8Array([1, 0xff]),
      fieldName: "text",
    },
    {
      layoutUnderTest: layout({
        name: "Utf8Message",
        isMessage: true,
        minimumSize: 1,
        fields: [
          field({ id: 0, name: "textLen", kind: "unsigned", widthBytes: 1 }),
          field({ id: 1, name: "text", kind: "string", stringEncoding: "utf8", dynamicSize: true, sizeFromField: 0 }),
        ],
      }),
      frame: new Uint8Array([1, 0xff]),
      fieldName: "text",
    },
    {
      layoutUnderTest: layout({
        name: "ReservedMessage",
        isMessage: true,
        minimumSize: 2,
        fields: [field({ id: 0, name: "reserved", kind: "bytes", fixedSize: 2, isReserved: true, reservedFillByte: reservedFill })],
      }),
      frame: new Uint8Array([reservedFill, 0]),
      fieldName: "reserved",
    },
  ];

  for (const { layoutUnderTest, frame, fieldName } of cases) {
    const errorCodec = new Codec({
      name: layoutUnderTest.name,
      fingerprint: "3",
      structs: [],
      messages: [layoutUnderTest],
    });

    assert.throws(
      () => errorCodec.decode(layoutUnderTest.name, frame),
      (error: unknown) => {
        assert.ok(error instanceof DecodeError);
        assert.equal(error.fieldName, fieldName);
        assert.equal(error.status, "invalid_data");
        return true;
      },
    );
  }
});

test("decode prefixes collection errors and rejects zero-width elements", () => {
  const collectionMessage = layout({
    name: "CollectionMessage",
    isMessage: true,
    minimumSize: 1,
    fields: [
      field({ id: 0, name: "count", kind: "unsigned", widthBytes: 1 }),
      field({ id: 1, name: "items", kind: "collection", structId: 3, dynamicCount: true, countFromField: 0 }),
    ],
  });
  const zeroCollectionMessage = layout({
    name: "ZeroCollection",
    isMessage: true,
    fields: [field({ id: 0, name: "items", kind: "collection", structId: 4, fixedCount: 1 })],
  });
  const collectionCodec = new Codec({
    name: "collection_errors",
    fingerprint: "4",
    structs: matrixProtocol.structs,
    messages: [collectionMessage, zeroCollectionMessage],
  });

  assert.throws(
    () => collectionCodec.decode("CollectionMessage", new Uint8Array([1])),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.fieldName, "items[0].value");
      return true;
    },
  );
  assert.throws(
    () => collectionCodec.decode("ZeroCollection", new Uint8Array(0)),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.fieldName, "items");
      return true;
    },
  );
});

test("decode rejects unknown variants and trailing bytes", () => {
  const validFrame = matrixCodec.encode("Matrix", matrixValue());
  const unknownVariantFrame = Uint8Array.from(validFrame);
  const kindOffset = validFrame.findIndex((byte, index) => byte === quoteKind && validFrame[index + 1] === quoteBid);
  assert.ok(kindOffset >= 0);
  unknownVariantFrame[kindOffset] = unknownKind;

  assert.throws(
    () => matrixCodec.decode("Matrix", unknownVariantFrame),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.fieldName, "detail");
      return true;
    },
  );
  assert.throws(() => matrixCodec.decode("Matrix", Uint8Array.from([...validFrame, 0])), DecodeError);

  const structFrame = matrixCodec.encode("MatrixPayload", matrixValue().payload as Record<string, unknown>);
  assert.throws(() => matrixCodec.decode("MatrixPayload", Uint8Array.from([...structFrame, 0])), DecodeError);
});

test("codec covers scalar coercion and unsupported metadata errors", () => {
  assert.throws(() => matrixCodec.encode("Matrix", { ...matrixValue(), aligned: Number.NaN }), EncodeError);

  const invalidBytesMessage = layout({
    name: "InvalidBytes",
    isMessage: true,
    minimumSize: 2,
    fields: [field({ id: 0, name: "blob", kind: "bytes", fixedSize: 2 })],
  });
  const unsupportedMessage = layout({
    name: "Unsupported",
    isMessage: true,
    minimumSize: 1,
    fields: [field({ id: 0, name: "mystery", kind: "unsupported" as Field["kind"], widthBytes: 1 })],
  });
  const errorCodec = new Codec({
    name: "metadata_errors",
    fingerprint: "5",
    structs: [],
    messages: [invalidBytesMessage, unsupportedMessage],
  });

  assert.throws(() => errorCodec.encode("InvalidBytes", { blob: 1 }), EncodeError);
  assert.throws(() => errorCodec.encode("Unsupported", { mystery: 1 }), EncodeError);
  assert.throws(() => errorCodec.decode("Unsupported", new Uint8Array([0])), DecodeError);
});

test("codec covers conditional gates and wide signed bigint decode", () => {
  const conditionalMessage = layout({
    name: "Conditional",
    isMessage: true,
    minimumSize: 1,
    fields: [
      field({ id: 0, name: "flag", kind: "unsigned", widthBytes: 1 }),
      field({ id: 1, name: "value", kind: "unsigned", widthBytes: 1, hasCondition: true, conditionField: 0, conditionEquals: 1 }),
    ],
  });
  const signedWideMessage = layout({
    name: "SignedWide",
    isMessage: true,
    minimumSize: 8,
    fields: [field({ id: 0, name: "value", kind: "signed", widthBytes: 8 })],
  });
  const gateCodec = new Codec({
    name: "gates",
    fingerprint: "6",
    structs: [],
    messages: [conditionalMessage, signedWideMessage],
  });

  assert.deepEqual(gateCodec.decode("Conditional", gateCodec.encode("Conditional", { flag: 0 })), { flag: 0 });
  assert.deepEqual(gateCodec.decode("Conditional", gateCodec.encode("Conditional", { flag: 1, value: 7 })), {
    flag: 1,
    value: 7,
  });
  assert.deepEqual(gateCodec.decode("SignedWide", new Uint8Array(Array(8).fill(0xff))), { value: -1n });
});

test("codec covers variant tag and nested non-decode failures", () => {
  const gatedTagMessage = layout({
    name: "GatedTagVariant",
    isMessage: true,
    fields: [
      field({ id: 0, name: "kind", kind: "unsigned", widthBytes: 1, hasPresence: true, presenceField: 0, presenceBit: 0 }),
      field({ id: 1, name: "detail", kind: "variant", tagFromField: 0, variantCases: [{ tagValue: quoteKind, structId: 1 }] }),
    ],
  });
  const brokenStructMessage = layout({
    name: "BrokenStruct",
    isMessage: true,
    minimumSize: 0,
    fields: [field({ id: 0, name: "missing", kind: "struct", structId: 99 })],
  });
  const brokenCodec = new Codec({
    name: "broken",
    fingerprint: "7",
    structs: [quoteStruct],
    messages: [gatedTagMessage, brokenStructMessage],
  });

  assert.throws(() => brokenCodec.encode("GatedTagVariant", { detail: { bid: quoteBid } }), EncodeError);
  assert.throws(() => brokenCodec.decode("BrokenStruct", new Uint8Array(0)), TypeError);
});

test("codec covers checksum anchor and decode schema failures", () => {
  const prefixedMessage = layout({
    name: "Prefixed",
    isMessage: true,
    minimumSize: 1,
    dispatchPrefix: new Uint8Array([0xaa]),
    fields: [field({ id: 0, name: "value", kind: "unsigned", widthBytes: 1 })],
  });
  const badAnchorMessage = layout({
    name: "BadAnchor",
    isMessage: true,
    minimumSize: 2,
    fields: [
      field({ id: 0, name: "value", kind: "unsigned", widthBytes: 1 }),
      field({ id: 1, name: "crc", kind: "unsigned", widthBytes: 1 }),
    ],
    checksums: [
      {
        fieldId: 1,
        resultWidthBytes: 1,
        algorithmName: "xor8",
        fromAnchor: { kind: "not_real" as never, fieldId: 0 },
        toAnchor: { kind: "before_self", fieldId: 1 },
      },
    ],
  });
  const reversedAnchorMessage = layout({
    name: "ReversedAnchor",
    isMessage: true,
    minimumSize: 2,
    fields: badAnchorMessage.fields,
    checksums: [
      {
        fieldId: 1,
        resultWidthBytes: 1,
        algorithmName: "xor8",
        fromAnchor: { kind: "frame_end", fieldId: 0 },
        toAnchor: { kind: "field_start", fieldId: 0 },
      },
    ],
  });
  const unknownChecksumMessage = layout({
    name: "UnknownChecksum",
    isMessage: true,
    minimumSize: 2,
    fields: badAnchorMessage.fields,
    checksums: [
      {
        fieldId: 1,
        resultWidthBytes: 1,
        algorithmName: "missing",
        fromAnchor: { kind: "frame_start", fieldId: 0 },
        toAnchor: { kind: "before_self", fieldId: 1 },
      },
    ],
  });
  const checksumCodec = new Codec({
    name: "checksum_errors",
    fingerprint: "8",
    structs: [],
    messages: [prefixedMessage, badAnchorMessage, reversedAnchorMessage, unknownChecksumMessage],
  });

  assert.throws(() => checksumCodec.decode("Prefixed", new Uint8Array([0xbb])), DecodeError);
  assert.throws(() => checksumCodec.encode("BadAnchor", { value: 1 }), EncodeError);
  assert.throws(() => checksumCodec.decode("BadAnchor", new Uint8Array([1, 0])), DecodeError);
  assert.throws(() => checksumCodec.decode("ReversedAnchor", new Uint8Array([1, 0])), DecodeError);
  assert.throws(() => checksumCodec.decode("UnknownChecksum", new Uint8Array([1, 0])), DecodeError);
});

test("codec covers field-end checksum anchors and fixed-size truncation", () => {
  const anchoredChecksumMessage = layout({
    name: "AnchoredChecksum",
    isMessage: true,
    minimumSize: 3,
    fields: [
      field({ id: 0, name: "value", kind: "unsigned", widthBytes: 1 }),
      field({ id: 1, name: "crc", kind: "unsigned", widthBytes: 2, byteOrder: "big_endian" }),
    ],
    checksums: [
      {
        fieldId: 1,
        resultWidthBytes: 2,
        algorithmName: "sum16",
        fromAnchor: { kind: "frame_start", fieldId: 0 },
        toAnchor: { kind: "field_end", fieldId: 0 },
      },
    ],
  });
  const fixedBytesMessage = layout({
    name: "FixedBytes",
    isMessage: true,
    minimumSize: 2,
    fields: [field({ id: 0, name: "blob", kind: "bytes", fixedSize: 2 })],
  });
  const edgeCodec = new Codec({
    name: "codec_edges",
    fingerprint: "10",
    structs: [],
    messages: [anchoredChecksumMessage, fixedBytesMessage],
  });

  assert.deepEqual(edgeCodec.encode("AnchoredChecksum", { value: 1 }), new Uint8Array([1, 0, 1]));
  assert.throws(() => edgeCodec.decode("FixedBytes", new Uint8Array([1])), DecodeError);
});

test("decodeSequence rejects zero-width records", () => {
  const emptyStruct = layout({ name: "EmptyRecord", fields: [] });
  const emptyCodec = new Codec({ name: "empty", fingerprint: "9", structs: [emptyStruct], messages: [] });

  assert.throws(() => emptyCodec.decodeSequence("EmptyRecord", new Uint8Array([0])), DecodeError);
});

test("round-trips a message with a collection and checksum", () => {
  const values = {
    count: 2,
    pairs: [
      { key: 1, value: 2 },
      { key: 3, value: 4 },
    ],
  };
  const frame = codec.encode("Bag", values);
  const decoded = codec.decode("Bag", frame);
  assert.equal(decoded.count, 2);
  assert.deepEqual(decoded.pairs, values.pairs);
});

test("rejects a corrupted checksum with rich context", () => {
  const frame = codec.encode("Bag", {
    count: 1,
    pairs: [{ key: 7, value: 9 }],
  });
  const corrupted = Uint8Array.from(frame);
  corrupted[corrupted.length - 1] ^= 0xff;
  assert.throws(
    () => codec.decode("Bag", corrupted),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.status, "checksum_mismatch");
      assert.equal(error.fieldName, "crc");
      return true;
    },
  );
});

test("reports collection field path and offset on impossible count", () => {
  // count says 1 pair, but only the key (2 bytes) of the pair is present.
  const truncated = new Uint8Array([1, 0x07, 0x00]);
  assert.throws(
    () => codec.decode("Bag", truncated),
    (error: unknown) => {
      assert.ok(error instanceof DecodeError);
      assert.equal(error.status, "schema_mismatch");
      assert.equal(error.fieldName, "pairs");
      return true;
    },
  );
});

test("decodeSequence decodes packed records", () => {
  const a = codec.encode("Pair", { key: 1, value: 2 });
  const b = codec.encode("Pair", { key: 3, value: 4 });
  const joined = new Uint8Array(a.length + b.length);
  joined.set(a, 0);
  joined.set(b, a.length);
  const records = codec.decodeSequence("Pair", joined);
  assert.equal(records.length, 2);
  assert.deepEqual(records[0], { key: 1, value: 2 });
  assert.deepEqual(records[1], { key: 3, value: 4 });
});

test("metadata isGated reflects gating flags", () => {
  assert.equal(metadata.isGated(field({ id: 0, name: "x", kind: "unsigned" })), false);
  assert.equal(
    metadata.isGated(field({ id: 0, name: "x", kind: "unsigned", hasPresence: true })),
    true,
  );
});

test("framing round-trips a payload", () => {
  const payload = new Uint8Array([1, 2, 3, 4, 5]);
  const frame = framing.encodeFrame(payload);
  const result = framing.tryReadFrame(frame);
  assert.ok(result);
  assert.deepEqual(Uint8Array.from(result.payload), payload);
  assert.equal(result.consumed, frame.length);
});

test("iterFrames yields complete prefix-width frames", () => {
  const first = framing.encodeFrame(frameOne, { prefixWidth: 1 });
  const second = framing.encodeFrame(frameTwo, { prefixWidth: 1 });
  const wire = new Uint8Array(first.length + second.length);
  wire.set(first, 0);
  wire.set(second, first.length);

  assert.deepEqual(Array.from(framing.iterFrames(wire, { prefixWidth: 1 })), [frameOne, frameTwo]);
});

test("framing rejects invalid options and oversized payloads", () => {
  const oversizedFrame = framing.encodeFrame(frameTwo);
  const operations = [
    () => framing.encodeFrame(new Uint8Array(0), { prefixWidth: invalidPrefixWidth as 1 }),
    () => framing.tryReadFrame(new Uint8Array(0), { prefixWidth: invalidPrefixWidth as 1 }),
    () => framing.encodeFrame(frameTwo, { maxPayload: maxTinyPayload }),
    () => framing.tryReadFrame(oversizedFrame, { maxPayload: maxTinyPayload }),
  ];

  for (const operation of operations) {
    assert.throws(operation, framing.FramingError);
  }
});

test("tryReadFrame reports partial inputs", () => {
  const wire = framing.encodeFrame(frameOne, { prefixWidth: 1 });

  assert.equal(framing.tryReadFrame(wire.subarray(0, 0), { prefixWidth: 1 }), null);
  assert.equal(framing.tryReadFrame(wire.subarray(0, wire.length - 1), { prefixWidth: 1 }), null);
});

test("FrameDecoder reassembles split frames", () => {
  const payload = new Uint8Array([9, 8, 7]);
  const frame = framing.encodeFrame(payload);
  const decoder = new framing.FrameDecoder();
  assert.deepEqual(decoder.feed(frame.subarray(0, 2)), []);
  const frames = decoder.feed(frame.subarray(2));
  assert.equal(frames.length, 1);
  assert.deepEqual(frames[0], payload);
});

test("handshake round-trips and validates compatibility", () => {
  const local = framing.defaultHandshake({ sessionId: 0x1122334455667788n });
  const encoded = framing.encodeHandshake(local);
  assert.equal(encoded.length, framing.HANDSHAKE_SIZE);
  const decoded = framing.decodeHandshake(encoded);
  assert.deepEqual(decoded, local);
  framing.checkCompatibility(local, decoded);
});

test("handshake rejects invalid transport modes", () => {
  const encoded = framing.encodeHandshake(framing.defaultHandshake());
  encoded[8] = 0xff;
  assert.throws(() => framing.decodeHandshake(encoded), framing.FramingError);
});

test("handshake rejects malformed payloads", () => {
  const badMagic = framing.encodeHandshake(framing.defaultHandshake());
  badMagic[0] = 0;

  assert.throws(() => framing.decodeHandshake(new Uint8Array(0)), framing.FramingError);
  assert.throws(() => framing.decodeHandshake(badMagic), framing.FramingError);
});

test("handshake rejects incompatible peers", () => {
  const local = framing.defaultHandshake();
  const remotes = [
    framing.defaultHandshake({ protocolVersion: local.protocolVersion + 1 }),
    framing.defaultHandshake({ transportMode: framing.TransportMode.Datagram }),
    framing.defaultHandshake({ frameCodec: local.frameCodec + 1 }),
    framing.defaultHandshake({ maxFrameBytes: maxTinyPayload }),
  ];

  for (const remote of remotes) {
    assert.throws(() => framing.checkCompatibility(local, remote), framing.FramingError);
  }
});
