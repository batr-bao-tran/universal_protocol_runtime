/**
 * Use runtime helpers that operate on raw wire bytes.
 *
 * Shows length-prefixed framing, streaming frame accumulation, UPR1 handshake
 * encoding/compatibility checks, and the built-in checksum algorithms.
 */

import {
  FrameDecoder,
  FramingError,
  TransportMode,
  checkCompatibility,
  checksums,
  decodeHandshake,
  defaultHandshake,
  encodeFrame,
  encodeHandshake,
  iterFrames,
  tryReadFrame,
} from "universal-protocol-runtime";

const TEXT = new TextEncoder();

function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function decode(bytes: Uint8Array): string {
  return new TextDecoder().decode(bytes);
}

function concat(parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((sum, p) => sum + p.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    out.set(part, offset);
    offset += part.length;
  }
  return out;
}

function demoFraming(): void {
  console.log("== framing ==");
  const payloads = ["alpha", "bravo", "charlie"].map((s) => TEXT.encode(s));

  // Default prefix width is 4 bytes (little-endian). Use prefixWidth 1/2 to match
  // a tighter wire format such as the hardware stream example.
  const wire = concat(payloads.map((p) => encodeFrame(p)));
  console.log(`wire_bytes=${wire.length} first_frame_hex=${toHex(encodeFrame(payloads[0]))}`);

  // One-shot: pull every complete frame out of a fully-buffered blob.
  console.log(`iter_frames=${[...iterFrames(wire)].map(decode).join(",")}`);

  // Single-frame parse returning { payload, consumed }, or null if the buffer
  // does not yet hold a whole frame.
  const head = tryReadFrame(wire);
  if (head === null) {
    throw new Error("expected a complete frame");
  }
  console.log(`try_read_frame payload=${decode(head.payload)} consumed=${head.consumed}`);

  // Streaming: a FrameDecoder reassembles frames from arbitrary chunk boundaries
  // (here we split mid-frame on purpose).
  const decoder = new FrameDecoder();
  const received: string[] = [];
  for (let index = 0; index < wire.length; index += 3) {
    for (const frame of decoder.feed(wire.subarray(index, index + 3))) {
      received.push(decode(frame));
    }
  }
  console.log(`frame_decoder=${received.join(",")}`);
}

function demoHandshake(): void {
  console.log("== session handshake ==");
  const local = defaultHandshake({
    transportMode: TransportMode.LengthPrefixedStream,
    maxFrameBytes: 1 << 20,
    sessionId: 0x1122334455667788n,
  });
  const blob = encodeHandshake(local);
  console.log(`handshake_bytes=${blob.length} hex=${toHex(blob)}`);

  const parsed = decodeHandshake(blob);
  console.log(`decoded transport_mode=${parsed.transportMode} session_id=0x${parsed.sessionId.toString(16)}`);

  // A peer that advertises a smaller max frame size is rejected.
  checkCompatibility(local, defaultHandshake({ maxFrameBytes: 1 << 20 }));
  console.log("compatible_peer=ok");

  try {
    checkCompatibility(local, defaultHandshake({ maxFrameBytes: 1024 }));
  } catch (error) {
    if (error instanceof FramingError) {
      console.log(`incompatible_peer rejected: ${error.message}`);
    } else {
      throw error;
    }
  }
}

function demoChecksums(): void {
  console.log("== checksums ==");
  const data = TEXT.encode("123456789"); // the canonical checksum test vector
  for (const algorithm of ["xor8", "sum16", "crc16_ccitt", "crc32", "crc32c"]) {
    const digest = checksums.compute(algorithm, data);
    console.log(`${algorithm.padStart(12)} = 0x${(digest >>> 0).toString(16).padStart(8, "0")}`);
  }
}

function main(): void {
  demoFraming();
  demoHandshake();
  demoChecksums();
}

main();
