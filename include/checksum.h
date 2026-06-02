#pragma once

// =============================================================================
// Lightweight CRC16 and MD5 for firmware upload integrity verification.
// No external dependencies — self-contained implementations.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

// =============================================================================
// CRC16-CCITT (polynomial 0x1021, init 0xFFFF)
// =============================================================================

class CRC16 {
public:
    CRC16() : crc_(0xFFFF) {}

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            crc_ ^= (uint16_t)data[i] << 8;
            for (int j = 0; j < 8; ++j) {
                if (crc_ & 0x8000)
                    crc_ = (crc_ << 1) ^ 0x1021;
                else
                    crc_ <<= 1;
            }
        }
    }

    void update(const std::string& s) {
        update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    uint16_t digest() const { return crc_; }

    // Return as 4-char hex string (e.g. "A1B2")
    std::string hex() const {
        char buf[5];
        snprintf(buf, sizeof(buf), "%04X", crc_);
        return buf;
    }

    void reset() { crc_ = 0xFFFF; }

private:
    uint16_t crc_;
};

// =============================================================================
// MD5 (RFC 1321) — compact implementation
// =============================================================================

class MD5 {
public:
    MD5() { reset(); }

    void reset() {
        state_[0] = 0x67452301;
        state_[1] = 0xEFCDAB89;
        state_[2] = 0x98BADCFE;
        state_[3] = 0x10325476;
        count_ = 0;
        memset(buffer_, 0, sizeof(buffer_));
    }

    void update(const uint8_t* data, size_t len) {
        size_t index = count_ & 0x3F;
        count_ += len;

        // Fill buffer first
        if (index) {
            size_t part_len = 64 - index;
            if (len >= part_len) {
                memcpy(buffer_ + index, data, part_len);
                transform(buffer_);
                data += part_len;
                len -= part_len;
                index = 0;
            } else {
                memcpy(buffer_ + index, data, len);
                return;
            }
        }

        // Process 64-byte blocks
        while (len >= 64) {
            transform(data);
            data += 64;
            len -= 64;
        }

        // Buffer remaining
        if (len > 0) {
            memcpy(buffer_, data, len);
        }
    }

    void update(const std::string& s) {
        update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Finalize and return 16-byte digest
    void finalize(uint8_t digest[16]) {
        uint8_t padding[64] = {0x80};
        size_t index = count_ & 0x3F;
        size_t pad_len = (index < 56) ? (56 - index) : (120 - index);

        uint64_t bits = count_ * 8;
        update(padding, pad_len);
        update(reinterpret_cast<const uint8_t*>(&bits), 8);

        for (int i = 0; i < 4; ++i) {
            digest[i * 4 + 0] = (uint8_t)(state_[i]);
            digest[i * 4 + 1] = (uint8_t)(state_[i] >> 8);
            digest[i * 4 + 2] = (uint8_t)(state_[i] >> 16);
            digest[i * 4 + 3] = (uint8_t)(state_[i] >> 24);
        }
    }

    // Return as 32-char hex string
    std::string hex() {
        uint8_t digest[16];
        finalize(digest);
        char buf[33];
        for (int i = 0; i < 16; ++i) {
            snprintf(buf + i * 2, 3, "%02x", digest[i]);
        }
        return std::string(buf, 32);
    }

private:
    uint32_t state_[4];
    uint64_t count_;
    uint8_t buffer_[64];

    static uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
    static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void transform(const uint8_t block[64]) {
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t M[16];

        for (int i = 0; i < 16; ++i) {
            M[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
                    ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
        }

        // Round 1
        #define FF(a,b,c,d,x,s,ac) { a += F(b,c,d) + x + ac; a = rotl(a,s) + b; }
        #define GG(a,b,c,d,x,s,ac) { a += G(b,c,d) + x + ac; a = rotl(a,s) + b; }
        #define HH(a,b,c,d,x,s,ac) { a += H(b,c,d) + x + ac; a = rotl(a,s) + b; }
        #define II(a,b,c,d,x,s,ac) { a += I(b,c,d) + x + ac; a = rotl(a,s) + b; }

        FF(a,b,c,d, M[ 0], 7, 0xD76AA478); FF(d,a,b,c, M[ 1],12, 0xE8C7B756);
        FF(c,d,a,b, M[ 2],17, 0x242070DB); FF(b,c,d,a, M[ 3],22, 0xC1BDCEEE);
        FF(a,b,c,d, M[ 4], 7, 0xF57C0FAF); FF(d,a,b,c, M[ 5],12, 0x4787C62A);
        FF(c,d,a,b, M[ 6],17, 0xA8304613); FF(b,c,d,a, M[ 7],22, 0xFD469501);
        FF(a,b,c,d, M[ 8], 7, 0x698098D8); FF(d,a,b,c, M[ 9],12, 0x8B44F7AF);
        FF(c,d,a,b, M[10],17, 0xFFFF5BB1); FF(b,c,d,a, M[11],22, 0x895CD7BE);
        FF(a,b,c,d, M[12], 7, 0x6B901122); FF(d,a,b,c, M[13],12, 0xFD987193);
        FF(c,d,a,b, M[14],17, 0xA679438E); FF(b,c,d,a, M[15],22, 0x49B40821);

        // Round 2
        GG(a,b,c,d, M[ 1], 5, 0xF61E2562); GG(d,a,b,c, M[ 6], 9, 0xC040B340);
        GG(c,d,a,b, M[11],14, 0x265E5A51); GG(b,c,d,a, M[ 0],20, 0xE9B6C7AA);
        GG(a,b,c,d, M[ 5], 5, 0xD62F105D); GG(d,a,b,c, M[10], 9, 0x02441453);
        GG(c,d,a,b, M[15],14, 0xD8A1E681); GG(b,c,d,a, M[ 4],20, 0xE7D3FBC8);
        GG(a,b,c,d, M[ 9], 5, 0x21E1CDE6); GG(d,a,b,c, M[14], 9, 0xC33707D6);
        GG(c,d,a,b, M[ 3],14, 0xF4D50D87); GG(b,c,d,a, M[ 8],20, 0x455A14ED);
        GG(a,b,c,d, M[13], 5, 0xA9E3E905); GG(d,a,b,c, M[ 2], 9, 0xFCEFA3F8);
        GG(c,d,a,b, M[ 7],14, 0x676F02D9); GG(b,c,d,a, M[12],20, 0x8D2A4C8A);

        // Round 3
        HH(a,b,c,d, M[ 5], 4, 0xFFFA3942); HH(d,a,b,c, M[ 8],11, 0x8771F681);
        HH(c,d,a,b, M[11],16, 0x6D9D6122); HH(b,c,d,a, M[14],23, 0xFDE5380C);
        HH(a,b,c,d, M[ 1], 4, 0xA4BEEA44); HH(d,a,b,c, M[ 4],11, 0x4BDECFA9);
        HH(c,d,a,b, M[ 7],16, 0xF6BB4B60); HH(b,c,d,a, M[10],23, 0xBEBFBC70);
        HH(a,b,c,d, M[13], 4, 0x289B7EC6); HH(d,a,b,c, M[ 0],11, 0xEAA127FA);
        HH(c,d,a,b, M[ 3],16, 0xD4EF3085); HH(b,c,d,a, M[ 6],23, 0x04881D05);
        HH(a,b,c,d, M[ 9], 4, 0xD9D4D039); HH(d,a,b,c, M[12],11, 0xE6DB99E5);
        HH(c,d,a,b, M[15],16, 0x1FA27CF8); HH(b,c,d,a, M[ 2],23, 0xC4AC5665);

        // Round 4
        II(a,b,c,d, M[ 0], 6, 0xF4292244); II(d,a,b,c, M[ 7],10, 0x432AFF97);
        II(c,d,a,b, M[14],15, 0xAB9423A7); II(b,c,d,a, M[ 5],21, 0xFC93A039);
        II(a,b,c,d, M[12], 6, 0x655B59C3); II(d,a,b,c, M[ 3],10, 0x8F0CCC92);
        II(c,d,a,b, M[10],15, 0xFFEFF47D); II(b,c,d,a, M[ 1],21, 0x85845DD1);
        II(a,b,c,d, M[ 8], 6, 0x6FA87E4F); II(d,a,b,c, M[15],10, 0xFE2CE6E0);
        II(c,d,a,b, M[ 6],15, 0xA3014314); II(b,c,d,a, M[13],21, 0x4E0811A1);
        II(a,b,c,d, M[ 4], 6, 0xF7537E82); II(d,a,b,c, M[11],10, 0xBD3AF235);
        II(c,d,a,b, M[ 2],15, 0x2AD7D2BB); II(b,c,d,a, M[ 9],21, 0xEB86D391);

        #undef FF
        #undef GG
        #undef HH
        #undef II

        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    }
};

// =============================================================================
// Convenience: compute CRC16 / MD5 over a buffer
// =============================================================================

inline uint16_t crc16_compute(const uint8_t* data, size_t len) {
    CRC16 crc;
    crc.update(data, len);
    return crc.digest();
}

inline std::string md5_compute(const uint8_t* data, size_t len) {
    MD5 md5;
    md5.update(data, len);
    return md5.hex();
}
