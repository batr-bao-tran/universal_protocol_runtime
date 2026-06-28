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
const BYTE_VALUE_COUNT = 256;
const DEFAULT_PREFIX_WIDTH = 4;
const PREFIX_WIDTH_UINT8 = 1;
const PREFIX_WIDTH_UINT16 = 2;
const PREFIX_WIDTH_UINT32 = 4;
const DEFAULT_MAX_FRAME_BYTES = 1 << 20; // 1 MiB, matching the default handshake limit.
const FRAME_DECODER_BUFFER_GROWTH_FACTOR = 2;
const HANDSHAKE_PROTOCOL_VERSION_OFFSET = 4;
const HANDSHAKE_FLAGS_OFFSET = 6;
const HANDSHAKE_TRANSPORT_MODE_OFFSET = 8;
const HANDSHAKE_FRAME_CODEC_OFFSET = 10;
const HANDSHAKE_MAX_FRAME_BYTES_OFFSET = 12;
const HANDSHAKE_SESSION_ID_OFFSET = 16;

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
    value = value * BYTE_VALUE_COUNT + data[offset + index];
  }
  return value;
}

function writeUintLE(view: DataView, offset: number, value: number, width: number): void {
  switch (width) {
    case PREFIX_WIDTH_UINT8:
      view.setUint8(offset, value);
      return;
    case PREFIX_WIDTH_UINT16:
      view.setUint16(offset, value, true);
      return;
    case PREFIX_WIDTH_UINT32:
      view.setUint32(offset, value, true);
      return;
    default:
      throw new FramingError(`unsupported prefix width ${width}`);
  }
}

function validatePrefixWidth(prefixWidth: number): asserts prefixWidth is 1 | 2 | 4 {
  if (
    prefixWidth !== PREFIX_WIDTH_UINT8 &&
    prefixWidth !== PREFIX_WIDTH_UINT16 &&
    prefixWidth !== PREFIX_WIDTH_UINT32
  ) {
    throw new FramingError(`unsupported prefix width ${prefixWidth}`);
  }
}

/** Wraps a payload in a little-endian length prefix. */
export function encodeFrame(payload: Uint8Array, options: FrameOptions = {}): Uint8Array {
  const prefixWidth = options.prefixWidth ?? DEFAULT_PREFIX_WIDTH;
  const maxPayload = options.maxPayload ?? DEFAULT_MAX_FRAME_BYTES;
  validatePrefixWidth(prefixWidth);
  if (payload.length > maxPayload) {
    throw new FramingError("payload exceeds max frame size");
  }
  const frame = new Uint8Array(prefixWidth + payload.length);
  writeUintLE(new DataView(frame.buffer), 0, payload.length, prefixWidth);
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
  const prefixWidth = options.prefixWidth ?? DEFAULT_PREFIX_WIDTH;
  const maxPayload = options.maxPayload ?? DEFAULT_MAX_FRAME_BYTES;
  validatePrefixWidth(prefixWidth);
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
  private readOffset = 0;
  private writeOffset = 0;

  constructor(options: FrameOptions = {}) {
    this.options = options;
  }

  private append(data: Uint8Array): void {
    if (data.length === 0) {
      return;
    }
    const unreadLength = this.writeOffset - this.readOffset;
    const requiredLength = unreadLength + data.length;
    if (this.buffer.length - this.writeOffset < data.length) {
      if (this.readOffset > 0 && this.buffer.length >= requiredLength) {
        this.buffer.copyWithin(0, this.readOffset, this.writeOffset);
      } else {
        let capacity = this.buffer.length === 0 ? requiredLength : this.buffer.length;
        while (capacity < requiredLength) {
          capacity = Math.max(capacity * FRAME_DECODER_BUFFER_GROWTH_FACTOR, requiredLength);
        }
        const next = new Uint8Array(capacity);
        next.set(this.buffer.subarray(this.readOffset, this.writeOffset));
        this.buffer = next;
      }
      this.readOffset = 0;
      this.writeOffset = unreadLength;
    }
    this.buffer.set(data, this.writeOffset);
    this.writeOffset += data.length;
  }

  private drainAvailable(available: Uint8Array): { frames: Uint8Array[]; consumed: number } {
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
    return { frames, consumed: offset };
  }

  /** Feeds received bytes and returns any newly completed frames. */
  feed(data: Uint8Array): Uint8Array[] {
    if (this.readOffset === this.writeOffset) {
      this.readOffset = 0;
      this.writeOffset = 0;
      const result = this.drainAvailable(data);
      if (result.consumed < data.length) {
        this.append(data.subarray(result.consumed));
      }
      return result.frames;
    }

    this.append(data);
    const result = this.drainAvailable(this.buffer.subarray(this.readOffset, this.writeOffset));
    this.readOffset += result.consumed;
    if (this.readOffset === this.writeOffset) {
      this.readOffset = 0;
      this.writeOffset = 0;
    }
    return result.frames;
  }
}

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
    maxFrameBytes: DEFAULT_MAX_FRAME_BYTES,
    sessionId: 0n,
    ...overrides,
  };
}

/** Encodes a handshake into its 24-byte payload. */
export function encodeHandshake(handshake: Handshake): Uint8Array {
  const out = new Uint8Array(HANDSHAKE_SIZE);
  const view = new DataView(out.buffer);
  out.set(HANDSHAKE_MAGIC, 0);
  view.setUint16(HANDSHAKE_PROTOCOL_VERSION_OFFSET, handshake.protocolVersion, true);
  view.setUint16(HANDSHAKE_FLAGS_OFFSET, handshake.flags, true);
  view.setUint16(HANDSHAKE_TRANSPORT_MODE_OFFSET, handshake.transportMode, true);
  view.setUint16(HANDSHAKE_FRAME_CODEC_OFFSET, handshake.frameCodec, true);
  view.setUint32(HANDSHAKE_MAX_FRAME_BYTES_OFFSET, handshake.maxFrameBytes, true);
  view.setBigUint64(HANDSHAKE_SESSION_ID_OFFSET, handshake.sessionId, true);
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
  const transportMode = view.getUint16(HANDSHAKE_TRANSPORT_MODE_OFFSET, true);
  if (
    transportMode !== TransportMode.LengthPrefixedStream &&
    transportMode !== TransportMode.DescriptorRing &&
    transportMode !== TransportMode.Datagram
  ) {
    throw new FramingError("UPR handshake transport mode is invalid");
  }
  return {
    protocolVersion: view.getUint16(HANDSHAKE_PROTOCOL_VERSION_OFFSET, true),
    flags: view.getUint16(HANDSHAKE_FLAGS_OFFSET, true),
    transportMode,
    frameCodec: view.getUint16(HANDSHAKE_FRAME_CODEC_OFFSET, true),
    maxFrameBytes: view.getUint32(HANDSHAKE_MAX_FRAME_BYTES_OFFSET, true),
    sessionId: view.getBigUint64(HANDSHAKE_SESSION_ID_OFFSET, true),
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
