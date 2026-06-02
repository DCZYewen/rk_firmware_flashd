// ============================================================
// test_checksum.cpp — unit tests for CRC16 and MD5.
//
// Uses official test vectors from RFCs and known values.
// ============================================================

#include "test_framework.h"
#include "checksum.h"
#include <cstring>

// =============================================================================
// CRC16-CCITT tests (polynomial 0x1021, init 0xFFFF)
// =============================================================================

TEST(crc16_empty) {
    CRC16 crc;
    ASSERT_EQ(crc.digest(), (uint16_t)0xFFFF);
    PASS();
}

TEST(crc16_single_byte_0x00) {
    // CRC16-CCITT of single byte 0x00 with init 0xFFFF
    CRC16 crc;
    uint8_t data[] = {0x00};
    crc.update(data, 1);
    // Expected: 0xE1F0
    ASSERT_EQ(crc.digest(), (uint16_t)0xE1F0);
    PASS();
}

TEST(crc16_single_byte_0xFF) {
    CRC16 crc;
    uint8_t data[] = {0xFF};
    crc.update(data, 1);
    // Expected: 0xFF00
    ASSERT_EQ(crc.digest(), (uint16_t)0xFF00);
    PASS();
}

TEST(crc16_known_string_123456789) {
    // Well-known CRC16-CCITT value for "123456789"
    CRC16 crc;
    crc.update(std::string("123456789"));
    // Expected: 0x29B1
    ASSERT_EQ(crc.digest(), (uint16_t)0x29B1);
    PASS();
}

TEST(crc16_incremental_matches_single) {
    // Feeding byte-by-byte should match feeding all at once
    const char* data = "Hello, World!";
    size_t len = strlen(data);

    CRC16 bulk;
    bulk.update(reinterpret_cast<const uint8_t*>(data), len);

    CRC16 incremental;
    for (size_t i = 0; i < len; ++i) {
        incremental.update(reinterpret_cast<const uint8_t*>(data + i), 1);
    }

    ASSERT_EQ(bulk.digest(), incremental.digest());
    PASS();
}

TEST(crc16_reset_works) {
    CRC16 crc;
    crc.update(std::string("test"));
    uint16_t first = crc.digest();

    crc.reset();
    crc.update(std::string("test"));
    uint16_t second = crc.digest();

    ASSERT_EQ(first, second);
    PASS();
}

TEST(crc16_hex_format) {
    CRC16 crc;
    crc.update(std::string("123456789"));
    ASSERT_EQ(crc.hex(), "29B1");
    PASS();
}

TEST(crc16_convenience_function) {
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    uint16_t result = crc16_compute(data, 9);
    ASSERT_EQ(result, (uint16_t)0x29B1);
    PASS();
}

// =============================================================================
// MD5 tests (RFC 1321 test vectors)
// =============================================================================

TEST(md5_empty_string) {
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    MD5 md5;
    std::string result = md5.hex();
    ASSERT_EQ(result, "d41d8cd98f00b204e9800998ecf8427e");
    PASS();
}

TEST(md5_a) {
    // MD5("a") = 0cc175b9c0f1b6a831c399e269772661
    MD5 md5;
    md5.update((const uint8_t*)"a", 1);
    ASSERT_EQ(md5.hex(), "0cc175b9c0f1b6a831c399e269772661");
    PASS();
}

TEST(md5_abc) {
    // MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
    MD5 md5;
    md5.update((const uint8_t*)"abc", 3);
    ASSERT_EQ(md5.hex(), "900150983cd24fb0d6963f7d28e17f72");
    PASS();
}

TEST(md5_message_digest) {
    // MD5("message digest") = f96b697d7cb7938d525a2f31aaf161d0
    MD5 md5;
    md5.update((const uint8_t*)"message digest", 14);
    ASSERT_EQ(md5.hex(), "f96b697d7cb7938d525a2f31aaf161d0");
    PASS();
}

TEST(md5_alphabet) {
    // MD5("abcdefghijklmnopqrstuvwxyz") = c3fcd3d76192e4007dfb496cca67e13b
    MD5 md5;
    md5.update((const uint8_t*)"abcdefghijklmnopqrstuvwxyz", 26);
    ASSERT_EQ(md5.hex(), "c3fcd3d76192e4007dfb496cca67e13b");
    PASS();
}

TEST(md5_alphanumeric) {
    // MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
    // = d174ab98d277d9f5a5611c2c9f419d9f
    MD5 md5;
    md5.update((const uint8_t*)"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62);
    ASSERT_EQ(md5.hex(), "d174ab98d277d9f5a5611c2c9f419d9f");
    PASS();
}

TEST(md5_numeric_with_special) {
    // MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
    // = 57edf4a22be3c955ac49da2e2107b67a
    MD5 md5;
    md5.update((const uint8_t*)"12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80);
    ASSERT_EQ(md5.hex(), "57edf4a22be3c955ac49da2e2107b67a");
    PASS();
}

TEST(md5_incremental_matches_single) {
    // Feeding byte-by-byte should match feeding all at once
    const char* data = "Hello, World!";
    size_t len = strlen(data);

    MD5 bulk;
    bulk.update(reinterpret_cast<const uint8_t*>(data), len);

    MD5 incremental;
    for (size_t i = 0; i < len; ++i) {
        incremental.update(reinterpret_cast<const uint8_t*>(data + i), 1);
    }

    ASSERT_EQ(bulk.hex(), incremental.hex());
    PASS();
}

TEST(md5_chunked_matches_single) {
    const char* data = "The quick brown fox jumps over the lazy dog";
    size_t len = strlen(data);

    MD5 all;
    all.update(reinterpret_cast<const uint8_t*>(data), len);

    MD5 chunked;
    // Feed in chunks of 7 bytes
    for (size_t i = 0; i < len; i += 7) {
        size_t chunk = (i + 7 <= len) ? 7 : (len - i);
        chunked.update(reinterpret_cast<const uint8_t*>(data + i), chunk);
    }

    ASSERT_EQ(all.hex(), chunked.hex());
    PASS();
}

TEST(md5_reset_works) {
    MD5 md5;
    md5.update((const uint8_t*)"test", 4);
    std::string first = md5.hex();

    md5.reset();
    md5.update((const uint8_t*)"test", 4);
    std::string second = md5.hex();

    ASSERT_EQ(first, second);
    PASS();
}

TEST(md5_convenience_function) {
    std::string result = md5_compute(
        reinterpret_cast<const uint8_t*>("abc"), 3);
    ASSERT_EQ(result, "900150983cd24fb0d6963f7d28e17f72");
    PASS();
}
