/**
 * Dependency-free, metadata-driven encoder/decoder. The algorithm mirrors the
 * C++ direct codec and Python runtime byte-for-byte, so frames are
 * interchangeable across languages.
 *
 * Values are plain objects:
 *   scalars            -> number (or bigint for 7/8-byte widths)
 *   float32/float64    -> number
 *   bytes              -> Uint8Array
 *   string             -> string
 *   struct             -> object
 *   collection         -> object[]
 *   variant            -> object (the active case's fields)
 *
 * Length/count fields are derived automatically from the data on encode.
 */

import * as checksums from "./checksums.js";
import { DecodeError, EncodeError } from "./errors.js";
import type { Checksum, ChecksumAnchor, Field, Layout, Protocol } from "./metadata.js";
import { isGated } from "./metadata.js";
import { TextDecoder, TextEncoder } from "node:util";

const SCALAR_KINDS = new Set(["unsigned", "signed", "float32", "float64", "enum"]);
const BYTE_MASK_BIGINT = 0xffn;
const BITS_PER_BYTE = 8;
const BITS_PER_BYTE_BIGINT = 8n;
// Six bytes (48 bits) is the widest unsigned integer that always fits safely in a JS number.
const MAX_INTEGER_WIDTH_BYTES_AS_NUMBER = 6;
// Used only when dynamic output outgrows an exact schema-derived starting size.
const MIN_ENCODE_BUFFER_CAPACITY = 64;
const ENCODE_BUFFER_GROWTH_FACTOR = 2;
// Keep spread calls comfortably below engine argument limits while decoding large ASCII fields.
const ASCII_DECODE_CHUNK_SIZE = 0x8000;

type Values = Record<string, unknown>;
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8", { fatal: true });

class ByteWriter {
  private buffer: Uint8Array;
  private view: DataView;
  length = 0;

  constructor(initialCapacity = 0) {
    this.buffer = new Uint8Array(initialCapacity);
    this.view = new DataView(this.buffer.buffer);
  }

  private ensureCapacity(requiredLength: number): void {
    if (requiredLength <= this.buffer.length) {
      return;
    }
    let capacity = Math.max(this.buffer.length, MIN_ENCODE_BUFFER_CAPACITY);
    while (capacity < requiredLength) {
      capacity = Math.max(capacity * ENCODE_BUFFER_GROWTH_FACTOR, requiredLength);
    }
    const next = new Uint8Array(capacity);
    next.set(this.buffer.subarray(0, this.length));
    this.buffer = next;
    this.view = new DataView(this.buffer.buffer);
  }

  private reserve(byteLength: number): number {
    const start = this.length;
    this.ensureCapacity(start + byteLength);
    this.length += byteLength;
    return start;
  }

  padTo(targetLength: number): void {
    if (targetLength <= this.length) {
      return;
    }
    const start = this.length;
    this.ensureCapacity(targetLength);
    this.buffer.fill(0, start, targetLength);
    this.length = targetLength;
  }

  writeBytes(bytes: ArrayLike<number>): void {
    const start = this.reserve(bytes.length);
    this.buffer.set(bytes, start);
  }

  writeRepeated(byte: number, count: number): void {
    const start = this.reserve(count);
    this.buffer.fill(byte, start, start + count);
  }

  writeFloat32(value: number, littleEndian: boolean): void {
    this.view.setFloat32(this.reserve(4), value, littleEndian);
  }

  writeFloat64(value: number, littleEndian: boolean): void {
    this.view.setFloat64(this.reserve(8), value, littleEndian);
  }

  writeUnsigned(value: bigint, widthBytes: number, littleEndian: boolean): void {
    this.patchUnsigned(this.reserve(widthBytes), value, widthBytes, littleEndian);
  }

  patchUnsigned(start: number, value: bigint, widthBytes: number, littleEndian: boolean): void {
    let v = value;
    if (littleEndian) {
      for (let index = 0; index < widthBytes; index++) {
        this.buffer[start + index] = Number(v & BYTE_MASK_BIGINT);
        v >>= BITS_PER_BYTE_BIGINT;
      }
      return;
    }
    for (let index = widthBytes - 1; index >= 0; index--) {
      this.buffer[start + index] = Number(v & BYTE_MASK_BIGINT);
      v >>= BITS_PER_BYTE_BIGINT;
    }
  }

  subarray(start: number, end: number): Uint8Array {
    return this.buffer.subarray(start, end);
  }

  toUint8Array(): Uint8Array {
    return this.length === this.buffer.length ? this.buffer : this.buffer.slice(0, this.length);
  }
}

function alignUp(value: number, alignment: number): number {
  if (alignment <= 1) {
    return value;
  }
  const remainder = value % alignment;
  return remainder === 0 ? value : value + (alignment - remainder);
}

function toUnsigned(value: number | bigint, widthBytes: number, fieldName?: string): bigint {
  const mod = 1n << BigInt(widthBytes * BITS_PER_BYTE);
  let v: bigint;
  if (typeof value === "bigint") {
    v = value;
  } else {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      throw new EncodeError("expected a finite numeric value", fieldName);
    }
    if (widthBytes > MAX_INTEGER_WIDTH_BYTES_AS_NUMBER && !Number.isSafeInteger(numeric)) {
      throw new EncodeError("integers wider than 6 bytes require bigint or a safe integer", fieldName);
    }
    v = BigInt(Math.trunc(numeric));
  }
  v = ((v % mod) + mod) % mod;
  return v;
}

function littleEndian(field: Field): boolean {
  return field.byteOrder !== "big_endian";
}

function validateAscii(data: Uint8Array): boolean {
  for (const byte of data) {
    if (byte >= 0x80) {
      return false;
    }
  }
  return true;
}

function decodeAscii(data: Uint8Array): string {
  let text = "";
  for (let offset = 0; offset < data.length; offset += ASCII_DECODE_CHUNK_SIZE) {
    text += String.fromCharCode(...data.subarray(offset, offset + ASCII_DECODE_CHUNK_SIZE));
  }
  return text;
}

function asBytes(field: Field, value: unknown): Uint8Array {
  if (typeof value === "string") {
    return textEncoder.encode(value);
  }
  if (value instanceof Uint8Array) {
    return value;
  }
  if (Array.isArray(value)) {
    return Uint8Array.from(value as number[]);
  }
  throw new EncodeError("expected bytes or string", field.name);
}

function fieldPresent(layout: Layout, field: Field, values: Values): boolean {
  if (field.hasCondition) {
    const other = layout.fields[field.conditionField];
    return Number(values[other.name] ?? 0) === field.conditionEquals;
  }
  if (field.hasPresence) {
    const other = layout.fields[field.presenceField];
    return ((Number(values[other.name] ?? 0) >> field.presenceBit) & 1) !== 0;
  }
  return true;
}

interface DerivedFields {
  lengths: Map<number, number>;
  payloads: Map<number, Uint8Array>;
}

function deriveFields(layout: Layout, values: Values): DerivedFields {
  const lengths = new Map<number, number>();
  const payloads = new Map<number, Uint8Array>();
  for (const field of layout.fields) {
    if (isGated(field) && !fieldPresent(layout, field, values)) {
      continue;
    }
    if ((field.kind === "bytes" || field.kind === "string") && field.dynamicSize) {
      const value = values[field.name];
      if (value !== undefined && value !== null) {
        const payload = asBytes(field, value);
        payloads.set(field.id, payload);
        lengths.set(field.sizeFromField, payload.length);
      }
    } else if (field.kind === "collection" && field.dynamicCount) {
      const value = values[field.name];
      if (Array.isArray(value)) {
        lengths.set(field.countFromField, value.length);
      }
    }
  }
  return { lengths, payloads };
}

function encodeScalar(writer: ByteWriter, field: Field, value: unknown): void {
  const le = littleEndian(field);
  if (field.kind === "float32") {
    writer.writeFloat32(Number(value), le);
    return;
  }
  if (field.kind === "float64") {
    writer.writeFloat64(Number(value), le);
    return;
  }
  writer.writeUnsigned(toUnsigned(value as number | bigint, field.widthBytes, field.name), field.widthBytes, le);
}

function encodeField(
  protocol: Protocol,
  writer: ByteWriter,
  layout: Layout,
  field: Field,
  value: unknown,
  values: Values,
  derived: DerivedFields,
): void {
  if (SCALAR_KINDS.has(field.kind)) {
    encodeScalar(writer, field, value);
    return;
  }
  if (field.kind === "bytes" || field.kind === "string") {
    const payload = derived.payloads.get(field.id) ?? asBytes(field, value);
    if (!field.dynamicSize && payload.length !== field.fixedSize) {
      throw new EncodeError(
        `fixed field expects ${field.fixedSize} bytes, got ${payload.length}`,
        field.name,
      );
    }
    writer.writeBytes(payload);
    return;
  }
  if (field.kind === "struct") {
    const nested = protocol.structs[field.structId];
    encodeLayoutInto(protocol, nested, (value ?? {}) as Values, writer);
    return;
  }
  if (field.kind === "collection") {
    const nested = protocol.structs[field.structId];
    for (const element of (value ?? []) as Values[]) {
      encodeLayoutInto(protocol, nested, element, writer);
    }
    return;
  }
  if (field.kind === "variant") {
    const tagField = layout.fields[field.tagFromField];
    if (!(tagField.name in values)) {
      throw new EncodeError("missing variant tag value", tagField.name);
    }
    const tag = Number(values[tagField.name]);
    for (const variantCase of field.variantCases) {
      if (variantCase.tagValue === tag) {
        const nested = protocol.structs[variantCase.structId];
        encodeLayoutInto(protocol, nested, (value ?? {}) as Values, writer);
        return;
      }
    }
    throw new EncodeError(`no variant case for tag ${tag}`, field.name);
  }
  throw new EncodeError(`unsupported field kind ${field.kind}`, field.name);
}

function anchorOffset(
  anchor: ChecksumAnchor,
  starts: Map<number, number>,
  ends: Map<number, number>,
  total: number,
): number {
  switch (anchor.kind) {
    case "frame_start":
      return 0;
    case "frame_end":
      return total;
    case "field_start":
    case "before_self":
      return starts.get(anchor.fieldId) ?? 0;
    case "field_end":
    case "after_self":
      return ends.get(anchor.fieldId) ?? 0;
    default:
      throw new EncodeError(`unknown checksum anchor ${anchor.kind}`);
  }
}

function encodeLayoutInto(protocol: Protocol, layout: Layout, values: Values, writer: ByteWriter): void {
  const layoutStart = writer.length;
  const derived = deriveFields(layout, values);
  const checksumFieldIds = new Set(layout.checksums.map((chk) => chk.fieldId));
  const fieldStarts = new Map<number, number>();
  const fieldEnds = new Map<number, number>();

  for (const field of layout.fields) {
    if (isGated(field) && !fieldPresent(layout, field, values)) {
      const relativeOffset = writer.length - layoutStart;
      fieldStarts.set(field.id, relativeOffset);
      fieldEnds.set(field.id, relativeOffset);
      continue;
    }
    writer.padTo(layoutStart + alignUp(writer.length - layoutStart, field.alignment));
    fieldStarts.set(field.id, writer.length - layoutStart);

    if (checksumFieldIds.has(field.id)) {
      writer.writeRepeated(0, field.widthBytes);
    } else if (derived.lengths.has(field.id)) {
      encodeScalar(writer, field, derived.lengths.get(field.id) as number);
    } else if (field.hasExpectedUnsigned) {
      encodeScalar(writer, field, field.expectedUnsigned);
    } else if (field.isReserved) {
      writer.writeRepeated(field.reservedFillByte, field.fixedSize);
    } else {
      if (!(field.name in values)) {
        throw new EncodeError("missing value", field.name);
      }
      encodeField(protocol, writer, layout, field, values[field.name], values, derived);
    }
    fieldEnds.set(field.id, writer.length - layoutStart);
  }

  for (const chk of layout.checksums) {
    const relativeLength = writer.length - layoutStart;
    const from = anchorOffset(chk.fromAnchor, fieldStarts, fieldEnds, relativeLength);
    const to = anchorOffset(chk.toAnchor, fieldStarts, fieldEnds, relativeLength);
    let digest: number;
    try {
      digest = checksums.compute(chk.algorithmName, writer.subarray(layoutStart + from, layoutStart + to));
    } catch (error) {
      throw new EncodeError((error as Error).message);
    }
    const chkField = layout.fields[chk.fieldId];
    const start = layoutStart + (fieldStarts.get(chk.fieldId) as number);
    writer.patchUnsigned(start, toUnsigned(digest, chkField.widthBytes), chkField.widthBytes, littleEndian(chkField));
  }
}

/** Encodes a value mapping into a wire frame. */
export function encode(protocol: Protocol, layout: Layout, values: Values): Uint8Array {
  const writer = new ByteWriter(layout.minimumSize);
  encodeLayoutInto(protocol, layout, values, writer);
  return writer.toUint8Array();
}

function readUint(frame: Uint8Array, offset: number, width: number, le: boolean): number | bigint {
  if (width <= MAX_INTEGER_WIDTH_BYTES_AS_NUMBER) {
    let value = 0;
    if (le) {
      for (let index = width - 1; index >= 0; index--) {
        value = value * 256 + frame[offset + index];
      }
    } else {
      for (let index = 0; index < width; index++) {
        value = value * 256 + frame[offset + index];
      }
    }
    return value;
  }
  let value = 0n;
  if (le) {
    for (let index = width - 1; index >= 0; index--) {
      value = (value << BITS_PER_BYTE_BIGINT) | BigInt(frame[offset + index]);
    }
  } else {
    for (let index = 0; index < width; index++) {
      value = (value << BITS_PER_BYTE_BIGINT) | BigInt(frame[offset + index]);
    }
  }
  return value;
}

function readScalar(frame: Uint8Array, offset: number, field: Field): number | bigint {
  const le = littleEndian(field);
  if (field.kind === "float32") {
    return new DataView(frame.buffer, frame.byteOffset + offset, 4).getFloat32(0, le);
  }
  if (field.kind === "float64") {
    return new DataView(frame.buffer, frame.byteOffset + offset, 8).getFloat64(0, le);
  }
  const unsigned = readUint(frame, offset, field.widthBytes, le);
  if (field.kind === "signed") {
    if (typeof unsigned === "bigint") {
      const bits = BigInt(field.widthBytes * BITS_PER_BYTE);
      const signBit = 1n << (bits - 1n);
      return unsigned >= signBit ? unsigned - (1n << bits) : unsigned;
    }
    const signBit = 2 ** (field.widthBytes * BITS_PER_BYTE - 1);
    return unsigned >= signBit ? unsigned - 2 ** (field.widthBytes * BITS_PER_BYTE) : unsigned;
  }
  return unsigned;
}

function prefixPath(parent: string, child: string): string {
  return child ? `${parent}.${child}` : parent;
}

function decodedPresent(layout: Layout, field: Field, out: Values): boolean {
  if (field.hasCondition) {
    const other = layout.fields[field.conditionField];
    return Number(out[other.name] ?? 0) === field.conditionEquals;
  }
  if (field.hasPresence) {
    const other = layout.fields[field.presenceField];
    return ((Number(out[other.name] ?? 0) >> field.presenceBit) & 1) !== 0;
  }
  return true;
}

interface DecodeResult {
  value: Values;
  consumed: number;
}

function decodeField(
  protocol: Protocol,
  layout: Layout,
  field: Field,
  frame: Uint8Array,
  base: number,
  offset: number,
  out: Values,
): number {
  const available = frame.length - base;

  if (SCALAR_KINDS.has(field.kind)) {
    if (offset + field.widthBytes > available) {
      throw new DecodeError("schema_mismatch", field.name, base + offset);
    }
    const value = readScalar(frame, base + offset, field);
    if (field.hasExpectedUnsigned && Number(value) !== field.expectedUnsigned) {
      throw new DecodeError("invalid_data", field.name, base + offset);
    }
    out[field.name] = value;
    return offset + field.widthBytes;
  }

  if (field.kind === "bytes" || field.kind === "string") {
    const size = field.dynamicSize
      ? Number(out[layout.fields[field.sizeFromField].name])
      : field.fixedSize;
    if (offset + size > available) {
      throw new DecodeError("schema_mismatch", field.name, base + offset);
    }
    const raw = frame.subarray(base + offset, base + offset + size);
    if (field.kind === "string") {
      if (field.stringEncoding === "utf8") {
        try {
          out[field.name] = textDecoder.decode(raw);
        } catch {
          throw new DecodeError("invalid_data", field.name, base + offset);
        }
      } else {
        if (!validateAscii(raw)) {
          throw new DecodeError("invalid_data", field.name, base + offset);
        }
        out[field.name] = decodeAscii(raw);
      }
    } else {
      if (field.isReserved) {
        for (const byte of raw) {
          if (byte !== field.reservedFillByte) {
            throw new DecodeError("invalid_data", field.name, base + offset);
          }
        }
      }
      out[field.name] = Uint8Array.from(raw);
    }
    return offset + size;
  }

  if (field.kind === "struct") {
    const nested = protocol.structs[field.structId];
    const result = decodeLayoutPrefixed(protocol, nested, frame, base + offset, field.name);
    out[field.name] = result.value;
    return offset + result.consumed;
  }

  if (field.kind === "collection") {
    const nested = protocol.structs[field.structId];
    const count = field.dynamicCount
      ? Number(out[layout.fields[field.countFromField].name])
      : field.fixedCount;
    if (nested.minimumSize > 0 && count > Math.floor((available - offset) / nested.minimumSize)) {
      throw new DecodeError("schema_mismatch", field.name, base + offset);
    }
    const elements: Values[] = [];
    for (let index = 0; index < count; index++) {
      if (base + offset > frame.length) {
        throw new DecodeError("schema_mismatch", field.name, base + offset);
      }
      const result = decodeLayoutPrefixed(
        protocol,
        nested,
        frame,
        base + offset,
        `${field.name}[${index}]`,
      );
      if (result.consumed === 0) {
        throw new DecodeError("schema_mismatch", field.name, base + offset);
      }
      offset += result.consumed;
      elements.push(result.value);
    }
    out[field.name] = elements;
    return offset;
  }

  if (field.kind === "variant") {
    const tag = Number(out[layout.fields[field.tagFromField].name]);
    for (const variantCase of field.variantCases) {
      if (variantCase.tagValue === tag) {
        const nested = protocol.structs[variantCase.structId];
        const result = decodeLayoutPrefixed(protocol, nested, frame, base + offset, field.name);
        out[field.name] = result.value;
        return offset + result.consumed;
      }
    }
    throw new DecodeError("invalid_data", field.name, base + offset);
  }

  throw new DecodeError("invalid_data", field.name, base + offset);
}

function decodeLayoutPrefixed(
  protocol: Protocol,
  layout: Layout,
  frame: Uint8Array,
  base: number,
  pathPrefix: string,
): DecodeResult {
  try {
    return decodeLayout(protocol, layout, frame, base, false);
  } catch (error) {
    if (error instanceof DecodeError) {
      throw new DecodeError(error.status, prefixPath(pathPrefix, error.fieldName), error.byteOffset);
    }
    throw error;
  }
}

function decodeLayout(
  protocol: Protocol,
  layout: Layout,
  frame: Uint8Array,
  base: number,
  bounded: boolean,
): DecodeResult {
  const out: Values = {};
  let offset = 0;
  const fieldStarts = new Map<number, number>();
  const fieldEnds = new Map<number, number>();
  const available = frame.length - base;

  if (available < layout.minimumSize) {
    throw new DecodeError("schema_mismatch", "", base);
  }
  if (layout.isMessage && layout.dispatchPrefix.length > 0) {
    for (let index = 0; index < layout.dispatchPrefix.length; index++) {
      if (frame[base + index] !== layout.dispatchPrefix[index]) {
        throw new DecodeError("schema_mismatch", "", base);
      }
    }
  }

  for (const field of layout.fields) {
    if (isGated(field) && !decodedPresent(layout, field, out)) {
      fieldStarts.set(field.id, offset);
      fieldEnds.set(field.id, offset);
      continue;
    }
    offset = alignUp(offset, field.alignment);
    fieldStarts.set(field.id, offset);
    offset = decodeField(protocol, layout, field, frame, base, offset, out);
    fieldEnds.set(field.id, offset);
  }

  let checksumLimit: number;
  if (layout.isMessage) {
    if (!layout.allowTrailingBytes && offset !== available) {
      throw new DecodeError("schema_mismatch", "", base + offset);
    }
    checksumLimit = layout.allowTrailingBytes ? available : offset;
  } else {
    if (bounded && offset !== available) {
      throw new DecodeError("schema_mismatch", "", base + offset);
    }
    checksumLimit = offset;
  }

  for (const chk of layout.checksums) {
    const chkOffset = base + (fieldStarts.get(chk.fieldId) as number);
    const chkField = layout.fields[chk.fieldId];
    let from: number;
    let to: number;
    try {
      from = anchorOffset(chk.fromAnchor, fieldStarts, fieldEnds, checksumLimit);
      to = anchorOffset(chk.toAnchor, fieldStarts, fieldEnds, checksumLimit);
    } catch {
      throw new DecodeError("schema_mismatch", chkField.name, chkOffset);
    }
    if (from > to || to > checksumLimit) {
      throw new DecodeError("schema_mismatch", chkField.name, chkOffset);
    }
    let digest: number;
    try {
      digest = checksums.compute(chk.algorithmName, frame.subarray(base + from, base + to));
    } catch {
      throw new DecodeError("schema_mismatch", chkField.name, chkOffset);
    }
    if (Number(out[chkField.name]) !== digest) {
      throw new DecodeError("checksum_mismatch", chkField.name, chkOffset);
    }
  }

  return { value: out, consumed: offset };
}

/** Decodes a single frame into a value mapping. */
export function decode(protocol: Protocol, layout: Layout, frame: Uint8Array): Values {
  return decodeLayout(protocol, layout, frame, 0, true).value;
}

/** Decodes a packed sequence of records, tracking consumption internally. */
export function decodeSequence(protocol: Protocol, layout: Layout, frame: Uint8Array): Values[] {
  const records: Values[] = [];
  let offset = 0;
  while (offset < frame.length) {
    const result = decodeLayout(protocol, layout, frame, offset, false);
    if (result.consumed === 0) {
      throw new DecodeError("schema_mismatch", "", offset);
    }
    offset += result.consumed;
    records.push(result.value);
  }
  return records;
}
