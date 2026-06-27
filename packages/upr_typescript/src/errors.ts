/**
 * Error types shared by the TypeScript codec. These mirror the C++
 * `DecodeStatus`/`DecodeError` and `EncodeStatus` types for cross-language
 * consistency.
 */

export type DecodeStatus =
  | "ok"
  | "message_not_found"
  | "schema_mismatch"
  | "invalid_data"
  | "checksum_mismatch"
  | "field_limit_exceeded";

export class UprError extends Error {}

/** Rich decode failure carrying the failing field path and byte offset. */
export class DecodeError extends UprError {
  readonly status: DecodeStatus;
  readonly fieldName: string;
  readonly byteOffset: number;

  constructor(status: DecodeStatus, fieldName = "", byteOffset = 0) {
    const location = fieldName ? ` at field '${fieldName}'` : "";
    super(`decode failed (${status})${location} at byte offset ${byteOffset}`);
    this.name = "DecodeError";
    this.status = status;
    this.fieldName = fieldName;
    this.byteOffset = byteOffset;
  }
}

/** Raised when a value cannot be encoded against its schema. */
export class EncodeError extends UprError {
  readonly fieldName: string;

  constructor(message: string, fieldName = "") {
    const location = fieldName ? ` at field '${fieldName}'` : "";
    super(`encode failed${location}: ${message}`);
    this.name = "EncodeError";
    this.fieldName = fieldName;
  }
}
