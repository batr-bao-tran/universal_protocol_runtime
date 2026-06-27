/**
 * Schema descriptor types driving the metadata-driven codec. Generated protocol
 * modules build these objects; the codec walks them to encode/decode frames.
 */

export type FieldKind =
  | "unsigned"
  | "signed"
  | "float32"
  | "float64"
  | "bytes"
  | "string"
  | "struct"
  | "enum"
  | "collection"
  | "variant";

export type ByteOrder = "little_endian" | "big_endian";
export type StringEncoding = "ascii" | "utf8";
export type ChecksumAnchorKind =
  | "frame_start"
  | "frame_end"
  | "field_start"
  | "field_end"
  | "before_self"
  | "after_self";

export interface VariantCase {
  tagValue: number;
  structId: number;
}

export interface ChecksumAnchor {
  kind: ChecksumAnchorKind;
  fieldId: number;
}

export interface Checksum {
  fieldId: number;
  resultWidthBytes: number;
  algorithmName: string;
  fromAnchor: ChecksumAnchor;
  toAnchor: ChecksumAnchor;
}

export interface Field {
  id: number;
  name: string;
  kind: FieldKind;
  widthBytes: number;
  byteOrder: ByteOrder;
  stringEncoding: StringEncoding;
  fixedSize: number;
  dynamicSize: boolean;
  sizeFromField: number;
  structId: number;
  alignment: number;
  isReserved: boolean;
  reservedFillByte: number;
  fixedCount: number;
  dynamicCount: boolean;
  countFromField: number;
  hasCondition: boolean;
  conditionField: number;
  conditionEquals: number;
  hasPresence: boolean;
  presenceField: number;
  presenceBit: number;
  tagFromField: number;
  variantCases: VariantCase[];
  hasExpectedUnsigned: boolean;
  expectedUnsigned: number;
}

export interface Layout {
  name: string;
  isMessage: boolean;
  minimumSize: number;
  allowTrailingBytes: boolean;
  dispatchPrefix: Uint8Array;
  fields: Field[];
  checksums: Checksum[];
}

export interface Protocol {
  name: string;
  fingerprint: string;
  structs: Layout[];
  messages: Layout[];
}

export function isGated(field: Field): boolean {
  return field.hasCondition || field.hasPresence;
}
