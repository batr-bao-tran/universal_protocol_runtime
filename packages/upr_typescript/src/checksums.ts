/**
 * Pure-TypeScript implementations of the UPR built-in checksum algorithms,
 * byte-for-byte identical to the C++ and Python runtimes.
 */

const CRC16_CCITT_POLY = 0x8408;
const CRC32_POLY = 0xedb88320;
const CRC32C_POLY = 0x82f63b78;

function makeTable16(poly: number): Uint16Array {
  const table = new Uint16Array(256);
  for (let index = 0; index < 256; index++) {
    let crc = index;
    for (let bit = 0; bit < 8; bit++) {
      crc = crc & 1 ? (crc >>> 1) ^ poly : crc >>> 1;
    }
    table[index] = crc & 0xffff;
  }
  return table;
}

function makeTable32(poly: number): Uint32Array {
  const table = new Uint32Array(256);
  for (let index = 0; index < 256; index++) {
    let crc = index;
    for (let bit = 0; bit < 8; bit++) {
      crc = crc & 1 ? (crc >>> 1) ^ poly : crc >>> 1;
    }
    table[index] = crc >>> 0;
  }
  return table;
}

const CRC16_CCITT_TABLE = makeTable16(CRC16_CCITT_POLY);
const CRC32_TABLE = makeTable32(CRC32_POLY);
const CRC32C_TABLE = makeTable32(CRC32C_POLY);

export function xor8(data: Uint8Array): number {
  let value = 0;
  for (const byte of data) {
    value ^= byte;
  }
  return value & 0xff;
}

export function sum16(data: Uint8Array): number {
  let sum = 0;
  for (const byte of data) {
    sum = (sum + byte) & 0xffff;
  }
  return sum;
}

export function crc16Ccitt(data: Uint8Array): number {
  let crc = 0xffff;
  for (const byte of data) {
    crc = ((crc >>> 8) ^ CRC16_CCITT_TABLE[(crc ^ byte) & 0xff]) & 0xffff;
  }
  return ~crc & 0xffff;
}

export function crc32(data: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ byte) & 0xff];
  }
  return (~crc >>> 0) & 0xffffffff;
}

export function crc32c(data: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc = (crc >>> 8) ^ CRC32C_TABLE[(crc ^ byte) & 0xff];
  }
  return (~crc >>> 0) & 0xffffffff;
}

const BUILTINS: Record<string, (data: Uint8Array) => number> = {
  xor8,
  sum16,
  crc16_ccitt: crc16Ccitt,
  crc32,
  crc32c,
};

/** Computes a checksum by built-in algorithm name. */
export function compute(algorithmName: string, data: Uint8Array): number {
  const fn = BUILTINS[algorithmName];
  if (fn === undefined) {
    throw new Error(`unknown checksum algorithm '${algorithmName}'`);
  }
  return fn(data);
}
