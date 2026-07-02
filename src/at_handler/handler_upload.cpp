// =============================================================================
// handler_upload.cpp — AT+PREUPLOAD, AT+UPLOAD, AT+UPLOADDONE, AT+UPLOADCANCEL
//
// Upload state is stored in a function-local static so it persists across
// HTTP requests (AtCommand is created per-request).
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include "checksum.h"
#include <sstream>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

// =============================================================================
// Persistent upload state (same pattern as FlashOp in handler_flash.cpp)
// =============================================================================

struct UploadState {
    bool active = false;
    std::string filename;
    std::string tmp_path;
    uint64_t expected_size = 0;
    uint64_t received = 0;
    uint32_t frame_count = 0;
    FILE* file = nullptr;
    std::string expected_md5;
    MD5 md5_ctx;

    void reset() {
        active = false;
        filename.clear();
        tmp_path.clear();
        expected_size = 0;
        received = 0;
        frame_count = 0;
        expected_md5.clear();
        md5_ctx.reset();
    }
};

static UploadState& upload_state() {
    static UploadState s;
    return s;
}

// =============================================================================
// AT+PREUPLOAD — begin firmware upload with integrity check
// =============================================================================

std::string AtCommand::handle_preupload(const std::string& arg) {
    UploadState& u = upload_state();

    if (arg.empty()) {
        return "ERROR: AT+PREUPLOAD requires <file>,<size>,<md5>";
    }

    if (u.active) {
        return "ERROR: upload already in progress — send AT+UPLOADCANCEL first";
    }

    auto comma1 = arg.find(',');
    if (comma1 == std::string::npos) return "ERROR: missing size and md5";

    auto comma2 = arg.find(',', comma1 + 1);
    if (comma2 == std::string::npos) return "ERROR: missing md5";

    std::string filename = arg.substr(0, comma1);
    std::string size_str = arg.substr(comma1 + 1, comma2 - comma1 - 1);
    std::string md5_hex  = arg.substr(comma2 + 1);

    for (char c : size_str) {
        if (!isdigit(c)) return "ERROR: invalid size";
    }
    uint64_t total_size = strtoull(size_str.c_str(), nullptr, 10);
    if (total_size == 0) return "ERROR: size must be > 0";

    if (md5_hex.size() != 32) return "ERROR: md5 must be 32 hex chars";
    for (char c : md5_hex) {
        if (!isxdigit(c)) return "ERROR: invalid md5 hex";
    }

    if (filename.empty() || filename.size() > 255) return "ERROR: invalid filename";
    if (filename.find('/') != std::string::npos) return "ERROR: filename contains /";
    if (filename.find("..") != std::string::npos) return "ERROR: invalid filename";

    std::string tmp_name = "upload_" + filename + ".tmp";
    std::string tmp_path = daemon_.config().upload_dir + "/" + tmp_name;

    FILE* f = fopen(tmp_path.c_str(), "wb");
    if (!f) {
        LOG_ERROR("AT+PREUPLOAD: failed to create %s", tmp_path.c_str());
        return "ERROR: cannot create temp file";
    }

    u.active = true;
    u.filename = filename;
    u.tmp_path = tmp_path;
    u.expected_size = total_size;
    u.received = 0;
    u.frame_count = 0;
    u.file = f;
    u.expected_md5 = md5_hex;
    u.md5_ctx.reset();

    LOG_INFO("AT+PREUPLOAD: %s, %lu bytes, md5=%s",
             filename.c_str(), (unsigned long)total_size, md5_hex.c_str());
    return "OK READY";
}

// =============================================================================
// AT+UPLOAD — send a data frame with CRC16 verification
// =============================================================================

std::string AtCommand::handle_upload_frame(const std::string& arg) {
    UploadState& u = upload_state();

    if (arg.empty()) return "ERROR: AT+UPLOAD requires arguments";
    if (!u.active || !u.file) {
        return "ERROR: no active upload — send AT+PREUPLOAD first";
    }

    auto last_comma = arg.rfind(',');
    if (last_comma == std::string::npos) return "ERROR: missing crc16 and frame_len";

    auto prev_comma = arg.rfind(',', last_comma - 1);
    if (prev_comma == std::string::npos) return "ERROR: missing hex_data";

    std::string hex_data   = arg.substr(0, prev_comma);
    std::string crc16_hex  = arg.substr(prev_comma + 1, last_comma - prev_comma - 1);
    std::string len_str    = arg.substr(last_comma + 1);

    if (crc16_hex.size() != 4) return "ERROR: crc16 must be 4 hex chars";
    for (char c : crc16_hex) {
        if (!isxdigit(c)) return "ERROR: invalid crc16 hex";
    }

    for (char c : len_str) {
        if (!isdigit(c)) return "ERROR: invalid frame length";
    }
    uint32_t frame_len = strtoul(len_str.c_str(), nullptr, 10);

    if (hex_data.size() % 2 != 0) return "ERROR: hex data must have even length";
    size_t bin_len = hex_data.size() / 2;

    if (bin_len != frame_len) {
        std::ostringstream ss;
        ss << "ERROR: frame_len mismatch (declared=" << frame_len
           << " actual=" << bin_len << ")";
        return ss.str();
    }

    std::string bin_data(bin_len, '\0');
    for (size_t i = 0; i < bin_len; ++i) {
        unsigned int byte = 0;
        sscanf(hex_data.c_str() + i * 2, "%2x", &byte);
        bin_data[i] = (char)byte;
    }

    uint16_t computed_crc = crc16_compute(
        reinterpret_cast<const uint8_t*>(bin_data.data()), bin_len);
    uint16_t expected_crc = (uint16_t)strtoul(crc16_hex.c_str(), nullptr, 16);

    char computed_crc_buf[5];
    snprintf(computed_crc_buf, sizeof(computed_crc_buf), "%04X", computed_crc);
    std::string computed_crc_hex(computed_crc_buf);

    if (computed_crc != expected_crc) {
        LOG_WARN("AT+UPLOAD: CRC mismatch frame #%u (expected=%04X computed=%04X)",
                 u.frame_count, expected_crc, computed_crc);
        u.frame_count++;
        std::ostringstream ss;
        ss << "ERROR CRC mismatch frame " << (u.frame_count - 1)
           << " expected=" << crc16_hex
           << " got=" << computed_crc_hex;
        return ss.str();
    }

    size_t written = fwrite(bin_data.data(), 1, bin_len, u.file);
    if (written != bin_len) {
        LOG_ERROR("AT+UPLOAD: write failed (%zu != %zu)", written, bin_len);
        return "ERROR: write failed";
    }

    u.md5_ctx.update(
        reinterpret_cast<const uint8_t*>(bin_data.data()), bin_len);

    u.received += bin_len;
    u.frame_count++;

    LOG_DEBUG("AT+UPLOAD: frame #%u OK, %zu bytes (total %lu/%lu)",
              u.frame_count - 1, bin_len,
              (unsigned long)u.received,
              (unsigned long)u.expected_size);

    return "OK " + std::to_string(u.received);
}

// =============================================================================
// AT+UPLOADDONE — finalize upload, verify MD5
// =============================================================================

std::string AtCommand::handle_uploaddone() {
    UploadState& u = upload_state();

    if (!u.active || !u.file) {
        return "ERROR: no active upload";
    }

    fclose(u.file);
    u.file = nullptr;

    if (u.received != u.expected_size) {
        LOG_WARN("AT+UPLOADDONE: size mismatch (expected=%lu got=%lu)",
                 (unsigned long)u.expected_size,
                 (unsigned long)u.received);
        std::ostringstream ss;
        ss << "ERROR size mismatch expected=" << u.expected_size
           << " got=" << u.received;
        unlink(u.tmp_path.c_str());
        u.reset();
        return ss.str();
    }

    std::string actual_md5 = u.md5_ctx.hex();

    if (actual_md5 != u.expected_md5) {
        LOG_WARN("AT+UPLOADDONE: MD5 mismatch (expected=%s got=%s)",
                 u.expected_md5.c_str(), actual_md5.c_str());
        std::string result = "ERROR MD5 mismatch expected=" + u.expected_md5
                           + " got=" + actual_md5;
        unlink(u.tmp_path.c_str());
        u.reset();
        return result;
    }

    std::string final_path = daemon_.config().upload_dir + "/" + u.filename;
    if (rename(u.tmp_path.c_str(), final_path.c_str()) != 0) {
        LOG_ERROR("AT+UPLOADDONE: rename failed");
        unlink(u.tmp_path.c_str());
        u.reset();
        return "ERROR: failed to save firmware file";
    }

    LOG_INFO("AT+UPLOADDONE: OK %s %lu bytes, md5=%s",
             u.filename.c_str(),
             (unsigned long)u.received,
             actual_md5.c_str());

    std::string result = "OK " + u.filename + " "
                       + std::to_string(u.received) + " " + actual_md5;
    u.reset();
    return result;
}

// =============================================================================
// AT+UPLOADCANCEL — abort current upload
// =============================================================================

std::string AtCommand::handle_uploadcancel() {
    UploadState& u = upload_state();

    if (!u.active) {
        return "OK no upload in progress";
    }

    if (u.file) {
        fclose(u.file);
        u.file = nullptr;
    }
    unlink(u.tmp_path.c_str());

    LOG_INFO("AT+UPLOADCANCEL: upload of '%s' aborted (%lu bytes received)",
             u.filename.c_str(), (unsigned long)u.received);

    u.reset();
    return "OK";
}
