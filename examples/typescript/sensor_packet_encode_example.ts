/**
 * Encode hardware telemetry packets with derived fields.
 *
 * Shows expected constants, reserved/aligned bytes, automatic byte-length
 * fields, dynamic byte payloads, and a producer-side validation check.
 */

import { SensorPacket } from "./generated/hardware_telemetry.ts";

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

  // The validate() rule is a documented producer invariant; check it so
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
