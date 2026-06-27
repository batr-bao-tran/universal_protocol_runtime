/**
 * Length-prefixed framing and session handshake helpers. These match the C++
 * `FrameChannel` and `UprSession` wire formats (and the Python runtime), so a
 * TypeScript peer can interoperate with a C++ or Python peer. See
 * `docs/WIRE_SPEC.md` for the normative byte layout.
 *
 * Frame layout (stream transports):
 *
 *     [ length prefix : little-endian, prefixWidth bytes ][ payload ... ]
 *
 * The length prefix encodes the payload size in bytes (the prefix itself is not
 * counted). The default prefix width is 4 bytes.
 */

import { UprError } from "./errors.js";

const HANDSHAKE_MAGIC = new Uint8Array([0x55, 0x50, 0x52, 0x31]); // "UPR1"
export const HANDSHAKE_SIZE = 24;
const DEFAULT_MAX_PAYLOAD = 1 << 20;

export class FramingError extends UprError {
  constructor(message: string) {
    super(message);
    this.name = "FramingError";
  }
}

interface FrameOptions {
  prefixWidth?: 1 | 2 | 4;
  maxPayload?: number;
}

function readUintLE(data: Uint8Array, offset: number, width: number): number {
  let value = 0;
  for (let index = width - 1; index >= 0; index--) {
    value = value * 256 + data[offset + index];
  }
  return value;
}

function writeUintLE(value: number, width: number): Uint8Array {
  const bytes = new Uint8Array(width);
  let v = value;
  for (let index = 0; index < width; index++) {
    bytes[index] = v & 0xff;
    v = Math.floor(v / 256);
  }
  return bytes;
}

/** Wraps a payload in a little-endian length prefix. */
export function encodeFrame(payload: Uint8Array, options: FrameOptions = {}): Uint8Array {
  const prefixWidth = options.prefixWidth ?? 4;
  const maxPayload = options.maxPayload ?? DEFAULT_MAX_PAYLOAD;
  if (prefixWidth !== 1 && prefixWidth !== 2 && prefixWidth !== 4) {
    throw new FramingError(`unsupported prefix width ${prefixWidth}`);
  }
  if (payload.length > maxPayload) {
    throw new FramingError("payload exceeds max frame size");
  }
  const frame = new Uint8Array(prefixWidth + payload.length);
  frame.set(writeUintLE(payload.length, prefixWidth), 0);
  frame.set(payload, prefixWidth);
  return frame;
}

export interface ReadFrameResult {
  payload: Uint8Array;
  consumed: number;
}

/** Attempts to read a single frame from the front of `buffer`. */
export function tryReadFrame(
  buffer: Uint8Array,
  options: FrameOptions = {},
): ReadFrameResult | null {
  const prefixWidth = options.prefixWidth ?? 4;
  const maxPayload = options.maxPayload ?? DEFAULT_MAX_PAYLOAD;
  if (prefixWidth !== 1 && prefixWidth !== 2 && prefixWidth !== 4) {
    throw new FramingError(`unsupported prefix width ${prefixWidth}`);
  }
  if (buffer.length < prefixWidth) {
    return null;
  }
  const payloadSize = readUintLE(buffer, 0, prefixWidth);
  if (payloadSize > maxPayload) {
    throw new FramingError("frame exceeds configured max frame size");
  }
  const total = prefixWidth + payloadSize;
  if (buffer.length < total) {
    return null;
  }
  return { payload: buffer.subarray(prefixWidth, total), consumed: total };
}

/** Yields every complete frame contained in `buffer`. */
export function* iterFrames(
  buffer: Uint8Array,
  options: FrameOptions = {},
): Generator<Uint8Array> {
  let offset = 0;
  while (true) {
    const result = tryReadFrame(buffer.subarray(offset), options);
    if (result === null) {
      return;
    }
    offset += result.consumed;
    yield result.payload;
  }
}

/** Stateful accumulator that yields frames as bytes arrive. */
export class FrameDecoder {
  private readonly options: FrameOptions;
  private buffer = new Uint8Array(0);

  constructor(options: FrameOptions = {}) {
    this.options = options;
  }

  /** Feeds received bytes and returns any newly completed frames. */
  feed(data: Uint8Array): Uint8Array[] {
    let available: Uint8Array;
    if (this.buffer.length === 0) {
      available = data;
    } else {
      available = new Uint8Array(this.buffer.length + data.length);
      available.set(this.buffer, 0);
      available.set(data, this.buffer.length);
    }

    const frames: Uint8Array[] = [];
    let offset = 0;
    while (true) {
      const result = tryReadFrame(available.subarray(offset), this.options);
      if (result === null) {
        break;
      }
      frames.push(Uint8Array.from(result.payload));
      offset += result.consumed;
    }
    this.buffer =
      offset === available.length ? new Uint8Array(0) : Uint8Array.from(available.subarray(offset));
    return frames;
  }
}

// --------------------------------------------------------------------------- //
// Session handshake
// --------------------------------------------------------------------------- //

export const TransportMode = {
  LengthPrefixedStream: 1,
  DescriptorRing: 2,
  Datagram: 3,
} as const;

export type TransportMode = (typeof TransportMode)[keyof typeof TransportMode];

export interface Handshake {
  protocolVersion: number;
  flags: number;
  transportMode: TransportMode;
  frameCodec: number;
  maxFrameBytes: number;
  sessionId: bigint;
}

export function defaultHandshake(overrides: Partial<Handshake> = {}): Handshake {
  return {
    protocolVersion: 1,
    flags: 0,
    transportMode: TransportMode.LengthPrefixedStream,
    frameCodec: 1,
    maxFrameBytes: 1 << 20,
    sessionId: 0n,
    ...overrides,
  };
}

/** Encodes a handshake into its 24-byte payload. */
export function encodeHandshake(handshake: Handshake): Uint8Array {
  const out = new Uint8Array(HANDSHAKE_SIZE);
  const view = new DataView(out.buffer);
  out.set(HANDSHAKE_MAGIC, 0);
  view.setUint16(4, handshake.protocolVersion, true);
  view.setUint16(6, handshake.flags, true);
  view.setUint16(8, handshake.transportMode, true);
  view.setUint16(10, handshake.frameCodec, true);
  view.setUint32(12, handshake.maxFrameBytes, true);
  view.setBigUint64(16, handshake.sessionId, true);
  return out;
}

/** Decodes a 24-byte handshake payload. */
export function decodeHandshake(payload: Uint8Array): Handshake {
  if (payload.length !== HANDSHAKE_SIZE) {
    throw new FramingError("UPR handshake frame has an unexpected size");
  }
  for (let index = 0; index < HANDSHAKE_MAGIC.length; index++) {
    if (payload[index] !== HANDSHAKE_MAGIC[index]) {
      throw new FramingError("UPR handshake magic is invalid");
    }
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const transportMode = view.getUint16(8, true);
  if (
    transportMode !== TransportMode.LengthPrefixedStream &&
    transportMode !== TransportMode.DescriptorRing &&
    transportMode !== TransportMode.Datagram
  ) {
    throw new FramingError("UPR handshake transport mode is invalid");
  }
  return {
    protocolVersion: view.getUint16(4, true),
    flags: view.getUint16(6, true),
    transportMode,
    frameCodec: view.getUint16(10, true),
    maxFrameBytes: view.getUint32(12, true),
    sessionId: view.getBigUint64(16, true),
  };
}

/** Validates that two handshakes are compatible (mirrors the C++ rules). */
export function checkCompatibility(local: Handshake, remote: Handshake): void {
  if (local.protocolVersion !== remote.protocolVersion) {
    throw new FramingError("UPR protocol versions do not match");
  }
  if (local.transportMode !== remote.transportMode) {
    throw new FramingError("UPR transport modes do not match");
  }
  if (local.frameCodec !== remote.frameCodec) {
    throw new FramingError("UPR frame codecs do not match");
  }
  if (remote.maxFrameBytes < local.maxFrameBytes) {
    throw new FramingError("Remote max frame size is smaller than the local requirement");
  }
}
