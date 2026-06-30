/**
 * TypeScript equivalent of `examples/cpp/src/sensor_packet_encode_example.cpp`.
 *
 * Encodes a `SensorPacket` from `examples/schema/hardware_telemetry.upr` and
 * round-trips it back through decode. This schema exercises the trickier
 * encoder features:
 *
 * - a reserved, alignment-padded gap (`pad: reserved[2] align(4)`) that the
 *   encoder fills automatically;
 * - a length-prefixed byte blob (`sample_bytes: bytes[sample_bytes_len]`) whose
 *   length field is derived from the data you supply - you never set
 *   `sample_bytes_len` by hand;
 * - a `validate(...)` rule documenting the producer invariant
 *   `sample_bytes_len == sample_count * 4` when `version == 2`.
 *
 * The C++ example also compares the *segmented / zero-copy* builder against the
 * contiguous builder. Zero-copy payload attachment is a C++-only encoder
 * optimisation; the TypeScript/Python runtimes always produce a single
 * contiguous frame, so this example focuses on the contiguous path (which is
 * byte-identical to the C++ contiguous output).
 */

import { CODEC, SensorPacket } from "./generated/hardware_telemetry.ts";

const SAMPLE_BYTES = Uint8Array.from([0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23]);

function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function bytesEqual(a: Uint8Array, b: Uint8Array): boolean {
  return a.length === b.length && a.every((value, index) => value === b[index]);
}

function main(): void {
  // We omit `message_type` (expected constant), `pad` (reserved) and
  // `sample_bytes_len` (derived from sample_bytes) - the codec supplies them.
  const packet: SensorPacket = { version: 2, sample_count: 2, sample_bytes: SAMPLE_BYTES };
  const encoded = SensorPacket.encode(packet);

  const decoded = SensorPacket.decode(encoded);
  if (decoded.sample_bytes_len !== SAMPLE_BYTES.length) {
    throw new Error("derived sample_bytes_len mismatch");
  }

  console.log(`sensor_packet_encoded bytes=${encoded.length} data=${Array.from(encoded).join(" ")}`);
  console.log(
    `decoded_sample_count=${decoded.sample_count} decoded_sample_bytes_len=${decoded.sample_bytes_len}`,
  );

  // The validate() rule is a documented producer invariant; mirror the check so
  // malformed packets are caught before they hit the wire.
  if (decoded.version === 2 && decoded.sample_bytes_len !== decoded.sample_count! * 4) {
    throw new Error("validate(sample_bytes_len == sample_count * 4) violated");
  }

  // Re-encoding the decoded value reproduces the frame byte-for-byte.
  const reencoded = SensorPacket.encode(decoded);
  console.log(`roundtrip_match=${bytesEqual(reencoded, encoded) ? "yes" : "no"}`);
  console.log(`sensor_packet_hex=${toHex(encoded)}`);
}

main();
