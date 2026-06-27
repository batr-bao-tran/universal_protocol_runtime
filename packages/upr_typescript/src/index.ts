/**
 * Universal Protocol Runtime — TypeScript/JavaScript runtime.
 *
 * Dependency-free encode/decode for UPR protocols, byte-compatible with the C++
 * and Python runtimes. Generated protocol modules import {@link Codec} and the
 * metadata types from this package.
 */

export * as checksums from "./checksums.js";
export * as codec from "./codec.js";
export * as framing from "./framing.js";
export * as metadata from "./metadata.js";

export { Codec } from "./runtime.js";
export { DecodeError, EncodeError, UprError } from "./errors.js";
export type { DecodeStatus } from "./errors.js";
export type {
  ByteOrder,
  Checksum,
  ChecksumAnchor,
  ChecksumAnchorKind,
  Field,
  FieldKind,
  Layout,
  Protocol,
  StringEncoding,
  VariantCase,
} from "./metadata.js";
export {
  FrameDecoder,
  FramingError,
  HANDSHAKE_SIZE,
  TransportMode,
  checkCompatibility,
  decodeHandshake,
  defaultHandshake,
  encodeFrame,
  encodeHandshake,
  iterFrames,
  tryReadFrame,
} from "./framing.js";
export type { Handshake, ReadFrameResult } from "./framing.js";
