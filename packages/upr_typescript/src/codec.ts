/**
 * Dependency-free, metadata-driven encoder/decoder. The algorithm mirrors the
 * C++ direct codec (and therefore the Python codec) byte-for-byte, so frames are
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

const SCALAR_KINDS = new Set(["unsigned", "signed", "float32", "float64", "enum"]);

type Values = Record<string, unknown>;
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8", { fatal: true });
const ASCII_DECODE_CHUNK_SIZE = 0x8000;

function alignUp(value: number, alignment: number): number {
  if (alignment <= 1) {
    return value;
  }
  const remainder = value % alignment;
  return remainder === 0 ? value : value + (alignment - remainder);
}

function toUnsigned(value: number | bigint, widthBytes: number, fieldName?: string): bigint {
  const mod = 1n << BigInt(widthBytes * 8);
  let v: bigint;
  if (typeof value === "bigint") {
    v = value;
  } else {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      throw new EncodeError("expected a finite numeric value", fieldName);
    }
    if (widthBytes > 6 && !Number.isSafeInteger(numeric)) {
      throw new EncodeError("7- and 8-byte integer fields require bigint or a safe integer", fieldName);
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

function appendBytes(out: number[], bytes: ArrayLike<number>): void {
  for (let index = 0; index < bytes.length; index++) {
    out.push(bytes[index]);
  }
}

function decodeAscii(data: Uint8Array): string {
  let text = "";
  for (let offset = 0; offset < data.length; offset += ASCII_DECODE_CHUNK_SIZE) {
    text += String.fromCharCode(...data.subarray(offset, offset + ASCII_DECODE_CHUNK_SIZE));
  }
  return text;
}

// --------------------------------------------------------------------------- //
// Encoding
// --------------------------------------------------------------------------- //

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

function derivedLengthFields(layout: Layout, values: Values): Map<number, number> {
  const derived = new Map<number, number>();
  for (const field of layout.fields) {
    if (isGated(field) && !fieldPresent(layout, field, values)) {
      continue;
    }
    if ((field.kind === "bytes" || field.kind === "string") && field.dynamicSize) {
      const value = values[field.name];
      if (value !== undefined && value !== null) {
        derived.set(field.sizeFromField, asBytes(field, value).length);
      }
    } else if (field.kind === "collection" && field.dynamicCount) {
      const value = values[field.name];
      if (Array.isArray(value)) {
        derived.set(field.countFromField, value.length);
      }
    }
  }
  return derived;
}

function encodeScalar(out: number[], field: Field, value: unknown): void {
  const le = littleEndian(field);
  if (field.kind === "float32") {
    const buffer = new Uint8Array(4);
    new DataView(buffer.buffer).setFloat32(0, Number(value), le);
    appendBytes(out, buffer);
    return;
  }
  if (field.kind === "float64") {
    const buffer = new Uint8Array(8);
    new DataView(buffer.buffer).setFloat64(0, Number(value), le);
    appendBytes(out, buffer);
    return;
  }
  let v = toUnsigned(value as number | bigint, field.widthBytes, field.name);
  const bytes = new Array<number>(field.widthBytes);
  for (let index = 0; index < field.widthBytes; index++) {
    bytes[index] = Number(v & 0xffn);
    v >>= 8n;
  }
  if (!le) {
    bytes.reverse();
  }
  appendBytes(out, bytes);
}

function isChecksumField(layout: Layout, fieldId: number): boolean {
  return layout.checksums.some((chk) => chk.fieldId === fieldId);
}

function encodeField(
  protocol: Protocol,
  out: number[],
  layout: Layout,
  field: Field,
  value: unknown,
  values: Values,
): void {
  if (SCALAR_KINDS.has(field.kind)) {
    encodeScalar(out, field, value);
    return;
  }
  if (field.kind === "bytes" || field.kind === "string") {
    const payload = asBytes(field, value);
    if (!field.dynamicSize && payload.length !== field.fixedSize) {
      throw new EncodeError(
        `fixed field expects ${field.fixedSize} bytes, got ${payload.length}`,
        field.name,
      );
    }
    appendBytes(out, payload);
    return;
  }
  if (field.kind === "struct") {
    const nested = protocol.structs[field.structId];
    appendBytes(out, encodeLayout(protocol, nested, (value ?? {}) as Values));
    return;
  }
  if (field.kind === "collection") {
    const nested = protocol.structs[field.structId];
    for (const element of (value ?? []) as Values[]) {
      appendBytes(out, encodeLayout(protocol, nested, element));
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
        appendBytes(out, encodeLayout(protocol, nested, (value ?? {}) as Values));
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

function encodeLayout(protocol: Protocol, layout: Layout, values: Values): number[] {
  const out: number[] = [];
  const derived = derivedLengthFields(layout, values);
  const fieldStarts = new Map<number, number>();
  const fieldEnds = new Map<number, number>();

  for (const field of layout.fields) {
    if (isGated(field) && !fieldPresent(layout, field, values)) {
      fieldStarts.set(field.id, out.length);
      fieldEnds.set(field.id, out.length);
      continue;
    }
    const aligned = alignUp(out.length, field.alignment);
    while (out.length < aligned) {
      out.push(0);
    }
    fieldStarts.set(field.id, out.length);

    if (isChecksumField(layout, field.id)) {
      for (let index = 0; index < field.widthBytes; index++) {
        out.push(0);
      }
    } else if (derived.has(field.id)) {
      encodeScalar(out, field, derived.get(field.id) as number);
    } else if (field.hasExpectedUnsigned) {
      encodeScalar(out, field, field.expectedUnsigned);
    } else if (field.isReserved) {
      for (let index = 0; index < field.fixedSize; index++) {
        out.push(field.reservedFillByte);
      }
    } else {
      if (!(field.name in values)) {
        throw new EncodeError("missing value", field.name);
      }
      encodeField(protocol, out, layout, field, values[field.name], values);
    }
    fieldEnds.set(field.id, out.length);
  }

  for (const chk of layout.checksums) {
    const from = anchorOffset(chk.fromAnchor, fieldStarts, fieldEnds, out.length);
    const to = anchorOffset(chk.toAnchor, fieldStarts, fieldEnds, out.length);
    let digest: number;
    try {
      digest = checksums.compute(chk.algorithmName, Uint8Array.from(out.slice(from, to)));
    } catch (error) {
      throw new EncodeError((error as Error).message);
    }
    const chkField = layout.fields[chk.fieldId];
    const start = fieldStarts.get(chk.fieldId) as number;
    const le = littleEndian(chkField);
    let v = toUnsigned(digest, chkField.widthBytes);
    const bytes = new Array<number>(chkField.widthBytes);
    for (let index = 0; index < chkField.widthBytes; index++) {
      bytes[index] = Number(v & 0xffn);
      v >>= 8n;
    }
    if (!le) {
      bytes.reverse();
    }
    for (let index = 0; index < bytes.length; index++) {
      out[start + index] = bytes[index];
    }
  }

  return out;
}

/** Encodes a value mapping into a wire frame. */
export function encode(protocol: Protocol, layout: Layout, values: Values): Uint8Array {
  return Uint8Array.from(encodeLayout(protocol, layout, values));
}

// --------------------------------------------------------------------------- //
// Decoding
// --------------------------------------------------------------------------- //

function readUint(frame: Uint8Array, offset: number, width: number, le: boolean): number | bigint {
  if (width <= 6) {
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
      value = (value << 8n) | BigInt(frame[offset + index]);
    }
  } else {
    for (let index = 0; index < width; index++) {
      value = (value << 8n) | BigInt(frame[offset + index]);
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
      const bits = BigInt(field.widthBytes * 8);
      const signBit = 1n << (bits - 1n);
      return unsigned >= signBit ? unsigned - (1n << bits) : unsigned;
    }
    const signBit = 2 ** (field.widthBytes * 8 - 1);
    return unsigned >= signBit ? unsigned - 2 ** (field.widthBytes * 8) : unsigned;
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
