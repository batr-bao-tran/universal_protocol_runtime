"""Pure-Python implementations of the UPR built-in checksum algorithms.

Every function is byte-for-byte identical to the C++ runtime implementation in
``direct_decode_support.hpp`` so that frames validate across languages.
"""

from __future__ import annotations

from typing import Callable, Dict, Union

BytesLike = Union[bytes, bytearray, memoryview]

_BYTE_VALUE_COUNT = 256
_BITS_PER_BYTE = 8
_LOW_BIT_MASK = 1
_BYTE_MASK = 0xFF
_UINT16_MASK = 0xFFFF
_UINT32_MASK = 0xFFFFFFFF
_CRC16_CCITT_POLY = 0x8408
_CRC32_POLY = 0xEDB88320
_CRC32C_POLY = 0x82F63B78
_CRC16_CCITT_INITIAL = _UINT16_MASK
_CRC32_INITIAL = _UINT32_MASK
_CRC16_WIDTH_BITS = 16
_CRC32_WIDTH_BITS = 32


def _make_crc_table(poly: int, width_bits: int) -> list[int]:
    mask = (1 << width_bits) - 1
    table: list[int] = []
    for index in range(_BYTE_VALUE_COUNT):
        crc = index
        for _ in range(_BITS_PER_BYTE):
            if crc & _LOW_BIT_MASK:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
        table.append(crc & mask)
    return table


_CRC16_CCITT_TABLE = _make_crc_table(_CRC16_CCITT_POLY, _CRC16_WIDTH_BITS)
_CRC32_TABLE = _make_crc_table(_CRC32_POLY, _CRC32_WIDTH_BITS)
_CRC32C_TABLE = _make_crc_table(_CRC32C_POLY, _CRC32_WIDTH_BITS)


def xor8(data: BytesLike) -> int:
    """Computes the XOR-8 checksum."""
    value = 0
    for byte in data:
        value ^= byte
    return value & _BYTE_MASK


def sum16(data: BytesLike) -> int:
    """Computes the 16-bit additive checksum."""
    return sum(data) & _UINT16_MASK


def crc16_ccitt(data: BytesLike) -> int:
    """Computes the reflected CRC-16/CCITT checksum (init 0xFFFF, xorout 0xFFFF)."""
    crc = _CRC16_CCITT_INITIAL
    for byte in data:
        crc = ((crc >> _BITS_PER_BYTE) ^ _CRC16_CCITT_TABLE[(crc ^ byte) & _BYTE_MASK]) & _UINT16_MASK
    return (~crc) & _UINT16_MASK


def crc32(data: BytesLike) -> int:
    """Computes the standard (zlib) CRC-32 checksum."""
    crc = _CRC32_INITIAL
    for byte in data:
        crc = (crc >> _BITS_PER_BYTE) ^ _CRC32_TABLE[(crc ^ byte) & _BYTE_MASK]
    return (~crc) & _UINT32_MASK


def crc32c(data: BytesLike) -> int:
    """Computes the CRC-32C (Castagnoli) checksum."""
    crc = _CRC32_INITIAL
    for byte in data:
        crc = (crc >> _BITS_PER_BYTE) ^ _CRC32C_TABLE[(crc ^ byte) & _BYTE_MASK]
    return (~crc) & _UINT32_MASK


BUILTIN_ALGORITHMS: Dict[str, Callable[[BytesLike], int]] = {
    "xor8": xor8,
    "sum16": sum16,
    "crc16_ccitt": crc16_ccitt,
    "crc32": crc32,
    "crc32c": crc32c,
}


def compute(algorithm_name: str, data: BytesLike) -> int:
    """Computes a checksum by built-in algorithm name.

    Args:
        algorithm_name: One of ``xor8``, ``sum16``, ``crc16_ccitt``, ``crc32`` or ``crc32c``.
        data: Bytes to checksum.

    Returns:
        The checksum value as an unsigned integer.

    Raises:
        KeyError: If the algorithm is not a known built-in.
    """
    return BUILTIN_ALGORITHMS[algorithm_name](data)
