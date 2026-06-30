/**
 * TypeScript equivalent of `examples/cpp/src/hardware_byte_stream_example.cpp`.
 *
 * The C++ example pushes two length-prefixed `SensorPacket` frames through a
 * `SpanTransport` (which hands out small chunks) into a `LengthPrefixedFramer`
 * (2-byte little-endian prefix) wired to a `StreamRuntime` that decodes each
 * reassembled frame.
 *
 * TypeScript has no `StreamRuntime`; instead the `framing` helpers provide a
 * `FrameDecoder` that accumulates bytes from a transport and yields complete
 * payloads. Using `prefixWidth: 2` makes the wire bytes identical to the C++
 * `LengthPrefixedFramer` configuration, so a TypeScript peer interoperates with
 * a C++ (or Python) peer.
 */

import { FrameDecoder, encodeFrame } from "universal-protocol-runtime";

import { CODEC } from "./generated/hardware_telemetry.ts";

// 2-byte little-endian length prefix, matching the C++ LengthPrefixedFramer.
const PREFIX_WIDTH = 2;
// Simulated transport read size, matching the C++ SpanTransport(..., 5).
const TRANSPORT_CHUNK = 5;

const FIRST_SAMPLES = Uint8Array.from([0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23]);
const SECOND_SAMPLES = Uint8Array.from([0x30, 0x31, 0x32, 0x33]);

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

function main(): void {
  const first = CODEC.encode("SensorPacket", { version: 2, sample_count: 2, sample_bytes: FIRST_SAMPLES });
  const second = CODEC.encode("SensorPacket", { version: 2, sample_count: 1, sample_bytes: SECOND_SAMPLES });

  // Build the on-wire stream: each packet wrapped in its length prefix.
  const stream = concat([
    encodeFrame(first, { prefixWidth: PREFIX_WIDTH }),
    encodeFrame(second, { prefixWidth: PREFIX_WIDTH }),
  ]);

  const decoder = new FrameDecoder({ prefixWidth: PREFIX_WIDTH });
  let decodedPackets = 0;
  let transportReads = 0;

  // Feed the stream in small chunks, exactly like reading from a socket/pipe.
  for (let offset = 0; offset < stream.length; offset += TRANSPORT_CHUNK) {
    const chunk = stream.subarray(offset, offset + TRANSPORT_CHUNK);
    transportReads += 1;
    for (const frame of decoder.feed(chunk)) {
      const message = CODEC.decode("SensorPacket", frame);
      console.log(
        `hardware_stream packet=${decodedPackets} ` +
          `sample_count=${message.sample_count} sample_bytes_len=${message.sample_bytes_len}`,
      );
      decodedPackets += 1;
    }
  }

  console.log(
    `hardware_stream_stats frames_decoded=${decodedPackets} ` +
      `transport_reads=${transportReads} bytes_read=${stream.length}`,
  );
  if (decodedPackets !== 2) {
    process.exitCode = 1;
  }
}

main();
