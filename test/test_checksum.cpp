// ============================================================
// test_checksum.cpp — unit tests for CRC16 and MD5.
//
// Uses official test vectors from RFCs and known values.
// ============================================================

#include "test_framework.h"
#include "checksum.h"
#include <cstring>
#include <cinttypes>

// ================================================================
// Helper: printf wrapper that prints to stdout (flushes immediately)
// ================================================================
#define PRINT_VEC(fmt, ...) do { \
    printf("    " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

// =============================================================================
// CRC16-CCITT tests (polynomial 0x1021, init 0xFFFF)
// =============================================================================

TEST(crc16_empty) {
    CRC16 crc;
    uint16_t actual = crc.digest();
    PRINT_VEC("CRC16 empty:   expected=0x%04X  actual=0x%04X", 0xFFFF, actual);
    ASSERT_EQ(actual, (uint16_t)0xFFFF);
    PASS();
}

TEST(crc16_single_byte_0x00) {
    // CRC16-CCITT of single byte 0x00 with init 0xFFFF
    CRC16 crc;
    uint8_t data[] = {0x00};
    crc.update(data, 1);
    uint16_t actual = crc.digest();
    PRINT_VEC("CRC16 0x00:    expected=0x%04X  actual=0x%04X", 0xE1F0, actual);
    ASSERT_EQ(actual, (uint16_t)0xE1F0);
    PASS();
}

TEST(crc16_single_byte_0xFF) {
    CRC16 crc;
    uint8_t data[] = {0xFF};
    crc.update(data, 1);
    uint16_t actual = crc.digest();
    PRINT_VEC("CRC16 0xFF:    expected=0x%04X  actual=0x%04X", 0xFF00, actual);
    ASSERT_EQ(actual, (uint16_t)0xFF00);
    PASS();
}

TEST(crc16_known_string_123456789) {
    // Well-known CRC16-CCITT value for "123456789"
    CRC16 crc;
    crc.update(std::string("123456789"));
    uint16_t actual = crc.digest();
    PRINT_VEC("CRC16 \"123456789\": expected=0x%04X  actual=0x%04X", 0x29B1, actual);
    ASSERT_EQ(actual, (uint16_t)0x29B1);
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

    uint16_t bulk_val = bulk.digest();
    uint16_t inc_val  = incremental.digest();
    PRINT_VEC("CRC16 incremental: bulk=0x%04X  incremental=0x%04X", bulk_val, inc_val);
    ASSERT_EQ(bulk_val, inc_val);
    PASS();
}

TEST(crc16_reset_works) {
    CRC16 crc;
    crc.update(std::string("test"));
    uint16_t first = crc.digest();

    crc.reset();
    crc.update(std::string("test"));
    uint16_t second = crc.digest();

    PRINT_VEC("CRC16 reset:    first=0x%04X  second=0x%04X", first, second);
    ASSERT_EQ(first, second);
    PASS();
}

TEST(crc16_hex_format) {
    CRC16 crc;
    crc.update(std::string("123456789"));
    std::string actual = crc.hex();
    PRINT_VEC("CRC16 hex:      expected=\"%s\"  actual=\"%s\"", "29B1", actual.c_str());
    ASSERT_EQ(actual, "29B1");
    PASS();
}

TEST(crc16_convenience_function) {
    uint8_t data[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
    uint16_t actual = crc16_compute(data, 9);
    PRINT_VEC("CRC16 compute(): expected=0x%04X  actual=0x%04X", 0x29B1, actual);
    ASSERT_EQ(actual, (uint16_t)0x29B1);
    PASS();
}

// =============================================================================
// MD5 tests (RFC 1321 test vectors)
// =============================================================================

TEST(md5_empty_string) {
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    MD5 md5;
    std::string actual = md5.hex();
    PRINT_VEC("MD5 \"\":        expected=%s  actual=%s",
              "d41d8cd98f00b204e9800998ecf8427e", actual.c_str());
    ASSERT_EQ(actual, "d41d8cd98f00b204e9800998ecf8427e");
    PASS();
}

TEST(md5_a) {
    // MD5("a") = 0cc175b9c0f1b6a831c399e269772661
    MD5 md5;
    md5.update((const uint8_t*)"a", 1);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 \"a\":       expected=%s  actual=%s",
              "0cc175b9c0f1b6a831c399e269772661", actual.c_str());
    ASSERT_EQ(actual, "0cc175b9c0f1b6a831c399e269772661");
    PASS();
}

TEST(md5_abc) {
    // MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
    MD5 md5;
    md5.update((const uint8_t*)"abc", 3);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 \"abc\":     expected=%s  actual=%s",
              "900150983cd24fb0d6963f7d28e17f72", actual.c_str());
    ASSERT_EQ(actual, "900150983cd24fb0d6963f7d28e17f72");
    PASS();
}

TEST(md5_message_digest) {
    // MD5("message digest") = f96b697d7cb7938d525a2f31aaf161d0
    MD5 md5;
    md5.update((const uint8_t*)"message digest", 14);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 \"message digest\": expected=%s  actual=%s",
              "f96b697d7cb7938d525a2f31aaf161d0", actual.c_str());
    ASSERT_EQ(actual, "f96b697d7cb7938d525a2f31aaf161d0");
    PASS();
}

TEST(md5_alphabet) {
    // MD5("abcdefghijklmnopqrstuvwxyz") = c3fcd3d76192e4007dfb496cca67e13b
    MD5 md5;
    md5.update((const uint8_t*)"abcdefghijklmnopqrstuvwxyz", 26);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 a-z:        expected=%s  actual=%s",
              "c3fcd3d76192e4007dfb496cca67e13b", actual.c_str());
    ASSERT_EQ(actual, "c3fcd3d76192e4007dfb496cca67e13b");
    PASS();
}

TEST(md5_alphanumeric) {
    // MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
    // = d174ab98d277d9f5a5611c2c9f419d9f
    MD5 md5;
    md5.update((const uint8_t*)"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 alphanum:   expected=%s  actual=%s",
              "d174ab98d277d9f5a5611c2c9f419d9f", actual.c_str());
    ASSERT_EQ(actual, "d174ab98d277d9f5a5611c2c9f419d9f");
    PASS();
}

TEST(md5_numeric_with_special) {
    // MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
    // = 57edf4a22be3c955ac49da2e2107b67a
    MD5 md5;
    md5.update((const uint8_t*)"12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80);
    std::string actual = md5.hex();
    PRINT_VEC("MD5 80 digits:  expected=%s  actual=%s",
              "57edf4a22be3c955ac49da2e2107b67a", actual.c_str());
    ASSERT_EQ(actual, "57edf4a22be3c955ac49da2e2107b67a");
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

    std::string bulk_val = bulk.hex();
    std::string inc_val  = incremental.hex();
    PRINT_VEC("MD5 incremental: bulk=%s  incremental=%s",
              bulk_val.c_str(), inc_val.c_str());
    ASSERT_EQ(bulk_val, inc_val);
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

    std::string all_val    = all.hex();
    std::string chunk_val  = chunked.hex();
    PRINT_VEC("MD5 chunked:    all=%s  chunked=%s",
              all_val.c_str(), chunk_val.c_str());
    ASSERT_EQ(all_val, chunk_val);
    PASS();
}

TEST(md5_reset_works) {
    MD5 md5;
    md5.update((const uint8_t*)"test", 4);
    std::string first = md5.hex();

    md5.reset();
    md5.update((const uint8_t*)"test", 4);
    std::string second = md5.hex();

    PRINT_VEC("MD5 reset:      first=%s  second=%s",
              first.c_str(), second.c_str());
    ASSERT_EQ(first, second);
    PASS();
}

TEST(md5_convenience_function) {
    std::string actual = md5_compute(
        reinterpret_cast<const uint8_t*>("abc"), 3);
    PRINT_VEC("MD5 compute():  expected=%s  actual=%s",
              "900150983cd24fb0d6963f7d28e17f72", actual.c_str());
    ASSERT_EQ(actual, "900150983cd24fb0d6963f7d28e17f72");
    PASS();
}
