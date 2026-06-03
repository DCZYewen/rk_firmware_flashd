// =============================================================================
// handler_crypto.cpp — AT+DECRYPT, AT+VERIFY
//
// AT+DECRYPT requires AES256 key file on disk.
// AT+VERIFY computes MD5 of a file for integrity check.
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include "checksum.h"

extern "C" {
#include "aes.h"
}

#include <sstream>
#include <cstdio>
#include <cstring>
#include <unistd.h>

// Default key file path (on device, saved by provisioning tool)
static const char KEY_FILE_PATH[] = "/etc/rk_flashd/key.bin";

// =============================================================================
// AT+VERIFY=<file> — compute MD5 of a file
//
// Returns: OK <md5hex>
// =============================================================================

std::string AtCommand::handle_verify(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+VERIFY requires a filename";
    }

    // Validate filename (no path traversal)
    if (arg.find("..") != std::string::npos) {
        return "ERROR: invalid filename";
    }

    // Build full path
    std::string filepath = daemon_.config().upload_dir + "/" + arg;

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        return "ERROR: cannot open file '" + arg + "'";
    }

    // Compute MD5
    MD5 md5;
    char buf[4096];
    size_t total = 0;

    while (size_t n = fread(buf, 1, sizeof(buf), f)) {
        md5.update(reinterpret_cast<const uint8_t*>(buf), n);
        total += n;
    }
    fclose(f);

    std::string md5hex = md5.hex();
    LOG_INFO("AT+VERIFY: %s = %s (%zu bytes)", arg.c_str(), md5hex.c_str(), total);

    return "OK " + md5hex + " " + std::to_string(total);
}

// =============================================================================
// AT+DECRYPT=<input>,<output> — decrypt firmware file with AES256-CBC
//
// Uses key from /etc/rk_flashd/key.bin (16 bytes for AES-128, 32 for AES-256).
// If key file doesn't exist, returns error.
//
// The encrypted file format:
//   - First 16 bytes: IV (initialization vector)
//   - Remaining bytes: AES-CBC encrypted data (PKCS7 padded)
//
// Returns: OK <output> <original_size>
// =============================================================================

std::string AtCommand::handle_decrypt(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+DECRYPT requires <input>,<output>";
    }

    // Parse input,output
    auto comma = arg.find(',');
    if (comma == std::string::npos) {
        return "ERROR: AT+DECRYPT requires <input>,<output>";
    }

    std::string input = arg.substr(0, comma);
    std::string output = arg.substr(comma + 1);

    if (input.empty() || output.empty()) {
        return "ERROR: AT+DECRYPT requires <input>,<output>";
    }

    // Validate filenames
    if (input.find("..") != std::string::npos || output.find("..") != std::string::npos) {
        return "ERROR: invalid filename";
    }

    // Read encryption key
    FILE* keyfile = fopen(KEY_FILE_PATH, "rb");
    if (!keyfile) {
        return "ERROR: key file not found at " + std::string(KEY_FILE_PATH);
    }

    uint8_t key[32];
    size_t key_len = fread(key, 1, sizeof(key), keyfile);
    fclose(keyfile);

    if (key_len != 16 && key_len != 32) {
        return "ERROR: invalid key length (must be 16 or 32 bytes)";
    }

    // Open input file
    std::string in_path = daemon_.config().upload_dir + "/" + input;
    FILE* fin = fopen(in_path.c_str(), "rb");
    if (!fin) {
        return "ERROR: cannot open input file '" + input + "'";
    }

    // Read IV (first 16 bytes)
    uint8_t iv[16];
    if (fread(iv, 1, 16, fin) != 16) {
        fclose(fin);
        return "ERROR: file too small for IV";
    }

    // Read encrypted data
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 16, SEEK_SET);  // Skip IV

    size_t enc_size = file_size - 16;
    if (enc_size == 0 || enc_size % 16 != 0) {
        fclose(fin);
        return "ERROR: invalid encrypted file size";
    }

    uint8_t* enc_data = new uint8_t[enc_size];
    size_t nread = fread(enc_data, 1, enc_size, fin);
    fclose(fin);

    if (nread != enc_size) {
        delete[] enc_data;
        return "ERROR: failed to read encrypted data";
    }

    LOG_INFO("AT+DECRYPT: decrypting %s (%zu bytes)", input.c_str(), enc_size);

    // Decrypt with AES-CBC
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, enc_data, enc_size);

    // Remove PKCS7 padding
    if (enc_size == 0) {
        delete[] enc_data;
        return "ERROR: empty decrypted data";
    }

    uint8_t pad = enc_data[enc_size - 1];
    if (pad < 1 || pad > 16) {
        delete[] enc_data;
        return "ERROR: invalid PKCS7 padding";
    }

    // Verify all padding bytes
    for (size_t i = enc_size - pad; i < enc_size; ++i) {
        if (enc_data[i] != pad) {
            delete[] enc_data;
            return "ERROR: invalid PKCS7 padding";
        }
    }

    size_t orig_size = enc_size - pad;

    // Write decrypted file
    std::string out_path = daemon_.config().upload_dir + "/" + output;
    FILE* fout = fopen(out_path.c_str(), "wb");
    if (!fout) {
        delete[] enc_data;
        return "ERROR: cannot create output file";
    }

    fwrite(enc_data, 1, orig_size, fout);
    fclose(fout);
    delete[] enc_data;

    LOG_INFO("AT+DECRYPT: %s -> %s (%zu bytes)", input.c_str(), output.c_str(), orig_size);
    return "OK " + output + " " + std::to_string(orig_size);
}
