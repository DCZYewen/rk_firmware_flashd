// ============================================================
// test_aes.cpp — unit tests for AES encrypt/decrypt roundtrip.
//
// Verifies that tiny-AES-c encrypt → decrypt is reversible.
// Does NOT test the library's correctness against known vectors
// (the library already has its own tests for that).
// ============================================================

#include "test_framework.h"

// Override library default (AES128) → use AES-256 for all tests
#undef AES128
#define AES256 1

extern "C" {
#include "aes.h"
}
#include <cstring>

// =============================================================================
// ECB mode (simplest — no IV needed)
// =============================================================================

TEST(aes_ecb_roundtrip_single_block) {
    // AES-256 key (32 bytes)
    uint8_t key[32] = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13,
        0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B,
        0x1C, 0x1D, 0x1E, 0x1F
    };

    // Plaintext: exactly 16 bytes (one AES block)
    uint8_t plaintext[16] = {
        0x48, 0x65, 0x6C, 0x6C,  // "Hell"
        0x6F, 0x20, 0x57, 0x6F,  // "o Wo"
        0x72, 0x6C, 0x64, 0x21,  // "rld!"
        0x00, 0x00, 0x00, 0x00
    };

    // Copy for comparison after decrypt
    uint8_t original[16];
    memcpy(original, plaintext, 16);

    // Encrypt
    struct AES_ctx ctx;
    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, plaintext);

    // Encrypted data should differ from original
    ASSERT_TRUE(memcmp(plaintext, original, 16) != 0);

    // Decrypt
    AES_init_ctx(&ctx, key);
    AES_ECB_decrypt(&ctx, plaintext);

    // Should match original
    ASSERT_TRUE(memcmp(plaintext, original, 16) == 0);
    PASS();
}

TEST(aes_ecb_roundtrip_multiple_blocks) {
    uint8_t key[32] = {
        0x2B, 0x7E, 0x15, 0x16,
        0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88,
        0x09, 0xCF, 0x4F, 0x3C,
        0xA0, 0xA1, 0xA2, 0xA3,
        0xB0, 0xB1, 0xB2, 0xB3,
        0xC0, 0xC1, 0xC2, 0xC3,
        0xD0, 0xD1, 0xD2, 0xD3
    };

    // 4 blocks = 64 bytes
    size_t len = 64;
    uint8_t data[64];
    uint8_t original[64];

    // Fill with pattern
    for (size_t i = 0; i < len; ++i) {
        data[i] = (uint8_t)(i & 0xFF);
    }
    memcpy(original, data, len);

    // Encrypt block by block (ECB processes one block at a time)
    struct AES_ctx ctx;
    AES_init_ctx(&ctx, key);
    for (size_t i = 0; i < len; i += 16) {
        AES_ECB_encrypt(&ctx, data + i);
    }

    // Data should differ from original
    ASSERT_TRUE(memcmp(data, original, len) != 0);

    // Decrypt block by block
    AES_init_ctx(&ctx, key);
    for (size_t i = 0; i < len; i += 16) {
        AES_ECB_decrypt(&ctx, data + i);
    }

    // Should match original
    ASSERT_TRUE(memcmp(data, original, len) == 0);
    PASS();
}

TEST(aes_ecb_roundtrip_all_zeros) {
    uint8_t key[32] = {0};
    uint8_t data[16] = {0};
    uint8_t original[16] = {0};

    struct AES_ctx ctx;
    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, data);

    // Encrypted zeros should NOT be all zeros
    ASSERT_TRUE(memcmp(data, original, 16) != 0);

    AES_init_ctx(&ctx, key);
    AES_ECB_decrypt(&ctx, data);

    ASSERT_TRUE(memcmp(data, original, 16) == 0);
    PASS();
}

// =============================================================================
// CBC mode (with IV)
// =============================================================================

TEST(aes_cbc_roundtrip) {
    uint8_t key[32] = {
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13,
        0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B,
        0x1C, 0x1D, 0x1E, 0x1F
    };

    uint8_t iv[16] = {
        0xA0, 0xA1, 0xA2, 0xA3,
        0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB,
        0xAC, 0xAD, 0xAE, 0xAF
    };

    // 3 blocks = 48 bytes
    size_t len = 48;
    uint8_t data[48];
    uint8_t original[48];

    for (size_t i = 0; i < len; ++i) {
        data[i] = (uint8_t)(i + 0x20);
    }
    memcpy(original, data, len);

    // Encrypt
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_encrypt_buffer(&ctx, data, len);

    ASSERT_TRUE(memcmp(data, original, len) != 0);

    // Decrypt (must re-init with same IV)
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, data, len);

    ASSERT_TRUE(memcmp(data, original, len) == 0);
    PASS();
}

// =============================================================================
// CTR mode (stream cipher, no padding needed)
// =============================================================================

TEST(aes_ctr_roundtrip) {
    uint8_t key[32] = {
        0x2B, 0x7E, 0x15, 0x16,
        0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88,
        0x09, 0xCF, 0x4F, 0x3C,
        0xA0, 0xA1, 0xA2, 0xA3,
        0xB0, 0xB1, 0xB2, 0xB3,
        0xC0, 0xC1, 0xC2, 0xC3,
        0xD0, 0xD1, 0xD2, 0xD3
    };

    uint8_t iv[16] = {
        0xF0, 0xF1, 0xF2, 0xF3,
        0xF4, 0xF5, 0xF6, 0xF7,
        0xF8, 0xF9, 0xFA, 0xFB,
        0xFC, 0xFD, 0xFE, 0xFF
    };

    // 37 bytes (non-block-aligned, CTR handles this)
    size_t len = 37;
    uint8_t data[37];
    uint8_t original[37];

    for (size_t i = 0; i < len; ++i) {
        data[i] = (uint8_t)(i * 3 + 7);
    }
    memcpy(original, data, len);

    // Encrypt
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, data, len);

    ASSERT_TRUE(memcmp(data, original, len) != 0);

    // Decrypt (CTR encrypt and decrypt are the same operation)
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, data, len);

    ASSERT_TRUE(memcmp(data, original, len) == 0);
    PASS();
}

TEST(aes_ctr_same_input_same_output) {
    // Same key + same IV + same plaintext → same ciphertext
    uint8_t key[32] = {0x00};
    uint8_t iv[16]  = {0x01};
    uint8_t a[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                     0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t b[16];
    memcpy(b, a, 16);

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, a, 16);

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, b, 16);

    ASSERT_TRUE(memcmp(a, b, 16) == 0);
    PASS();
}
