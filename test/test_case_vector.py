#!/usr/bin/env python3
"""
test_case_vector.py — Verify all checksum test vectors used in test_checksum.cpp.

Uses Python's standard library (hashlib, binascii) to independently compute
CRC16-CCITT and MD5 digests and compare them against expected values.
"""

import sys
import hashlib
import binascii

# =============================================================================
# CRC16-CCITT (polynomial 0x1021, init 0xFFFF, no xor-out, reflect in/out)
# =============================================================================
# binascii.crc_hqx uses CRC-CCITT with polynomial 0x1021 and init 0x0000.
# Our implementation uses init 0xFFFF.  We can still verify by computing the
# CRC of the data and then XOR'ing with the expected crc of an empty string
# under init 0xFFFF.  Alternatively, we implement it directly for full
# transparency.
# =============================================================================

def crc16_ccitt(data: bytes) -> int:
    """Compute CRC16-CCITT (polynomial 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


CRC16_VECTORS = [
    # (label, data, expected_hex)
    ("empty",                b"",                        "FFFF"),
    ("single byte 0x00",    b"\x00",                    "E1F0"),
    ("single byte 0xFF",    b"\xFF",                    "FF00"),
    ("string '123456789'",  b"123456789",               "29B1"),
]

# =============================================================================
# MD5 (RFC 1321 test vectors)
# =============================================================================

MD5_VECTORS = [
    # (label, data, expected_hex)
    ("empty string",        b"",                                                                                                    "d41d8cd98f00b204e9800998ecf8427e"),
    ("'a'",                 b"a",                                                                                                   "0cc175b9c0f1b6a831c399e269772661"),
    ("'abc'",               b"abc",                                                                                                 "900150983cd24fb0d6963f7d28e17f72"),
    ("'message digest'",    b"message digest",                                                                                      "f96b697d7cb7938d525a2f31aaf161d0"),
    ("alphabet a-z",        b"abcdefghijklmnopqrstuvwxyz",                                                                          "c3fcd3d76192e4007dfb496cca67e13b"),
    ("alphanumeric A-Za-z0-9", b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",                                   "d174ab98d277d9f5a5611c2c9f419d9f"),
    ("80 digits",           b"12345678901234567890123456789012345678901234567890123456789012345678901234567890",                     "57edf4a22be3c955ac49da2e2107b67a"),
]


def run_crc16_tests() -> int:
    """Run all CRC16-CCITT test vectors. Returns number of failures."""
    failures = 0
    print("=" * 72)
    print("CRC16-CCITT  (polynomial=0x1021, init=0xFFFF)")
    print("=" * 72)
    for label, data, expected in CRC16_VECTORS:
        actual = crc16_ccitt(data)
        actual_hex = f"{actual:04X}"
        ok = actual_hex == expected.upper()
        status = "✓ PASS" if ok else "✗ FAIL"
        print(f"  {status}  {label:30s}  expected={expected:4s}  got={actual_hex:4s}")
        if not ok:
            failures += 1
    print()
    return failures


def run_md5_tests() -> int:
    """Run all MD5 test vectors. Returns number of failures."""
    failures = 0
    print("=" * 72)
    print("MD5  (RFC 1321 vectors)")
    print("=" * 72)
    for label, data, expected in MD5_VECTORS:
        actual = hashlib.md5(data).hexdigest()
        ok = actual == expected
        status = "✓ PASS" if ok else "✗ FAIL"
        print(f"  {status}  {label:30s}  expected={expected:32s}  got={actual:32s}")
        if not ok:
            failures += 1
    print()
    return failures


def main() -> int:
    crc_fail = run_crc16_tests()
    md5_fail = run_md5_tests()

    total = crc_fail + md5_fail
    if total == 0:
        print("All test vectors verified successfully.")
    else:
        print(f"{total} test vector(s) FAILED verification!", file=sys.stderr)
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
