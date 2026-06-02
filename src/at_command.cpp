#include "at_command.h"
#include "logger.h"

#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

// =============================================================================
// Construction
// =============================================================================

AtCommand::AtCommand(SerialDaemon& daemon)
    : daemon_(daemon),
      start_time_(std::chrono::steady_clock::now()) {}

// =============================================================================
// process — main entry point
// =============================================================================

std::string AtCommand::process(const std::string& raw_cmd,
                               SerialDaemon::Owner caller,
                               int /*timeout_ms*/) {
    // Trim whitespace
    std::string cmd = raw_cmd;
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n' || cmd.back() == ' '))
        cmd.pop_back();

    LOG_INFO("AT process: '%s'", cmd.c_str());

    if (cmd.size() < 2 || cmd[0] != 'A' || cmd[1] != 'T') {
        return "ERROR: commands must start with AT";
    }

    std::string action, arg;
    if (!parse_at(cmd, action, arg)) {
        return "ERROR: invalid AT command format";
    }

    // --- All commands handled locally by the daemon ---
    if (action == "STATUS")       return handle_status();
    if (action == "RESET")        return handle_reset(caller);
    if (action == "FORCERESET")   return handle_forcereset();
    if (action == "HELP")         return handle_help();
    if (action == "FLASH")        return handle_flash(arg);
    if (action == "EXEC")         return handle_exec(arg);
    if (action == "PREUPLOAD")    return handle_preupload(arg);
    if (action == "UPLOAD")       return handle_upload_frame(arg);
    if (action == "UPLOADDONE")   return handle_uploaddone();
    if (action == "UPLOADCANCEL") return handle_uploadcancel();

    return "ERROR: unknown command 'AT+" + action + "'";
}

// =============================================================================
// Local command handlers
// =============================================================================

std::string AtCommand::handle_status() {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();

    std::ostringstream ss;
    ss << "OK"
       << " uptime=" << uptime << "s"
       << " serial=" << (daemon_.is_open() ? "open" : "closed")
       << " busy=" << (daemon_.is_busy() ? "yes" : "no")
       << " device=" << (daemon_.serial_enabled() ? daemon_.device() : "none");
    return ss.str();
}

std::string AtCommand::handle_reset(SerialDaemon::Owner caller) {
    if (daemon_.owner() != caller) {
        return "ERROR: only the lock owner can reset";
    }
    daemon_.release(caller);
    return "OK";
}

std::string AtCommand::handle_forcereset() {
    daemon_.force_release();
    return "OK";
}

std::string AtCommand::handle_help() {
    return "OK"
        " AT+STATUS                — daemon status"
        " AT+RESET                 — release session, reopen port"
        " AT+FORCERESET            — force-release lock (admin override)"
        " AT+FLASH=f               — flash firmware file"
        " AT+EXEC=cmd              — execute command (ALLOW_RCE only)"
        " AT+PREUPLOAD=f,n,md5     — begin upload (size + md5)"
        " AT+UPLOAD=data,crc16,len — send frame with CRC16"
        " AT+UPLOADDONE            — finalize, verify MD5"
        " AT+UPLOADCANCEL          — abort current upload"
        " AT+HELP                  — this help";
}

// =============================================================================
// AT+FLASH — execute flash script with parameter passthrough
// =============================================================================

static const char FLASH_SCRIPT[] = "/sbin/flash.sh";

std::string AtCommand::handle_flash(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+FLASH requires a filename";
    }

    // Parameter validation is the script's job — flashd just passes it through
    std::string cmd = std::string(FLASH_SCRIPT) + " " + arg;
    LOG_INFO("AT+FLASH: executing '%s'", cmd.c_str());

    int ret = std::system(cmd.c_str());
    if (ret == 0) {
        return "OK flash " + arg;
    }

    std::ostringstream ss;
    ss << "ERROR flash failed (exit code " << ret << ")";
    return ss.str();
}

// =============================================================================
// AT+EXEC — execute arbitrary command (requires ALLOW_RCE)
// =============================================================================

std::string AtCommand::handle_exec(const std::string& arg) {
#ifdef ALLOW_RCE
    if (arg.empty()) {
        return "ERROR: AT+EXEC requires a command";
    }

    LOG_WARN("AT+EXEC: executing '%s'", arg.c_str());

    FILE* pipe = popen(arg.c_str(), "r");
    if (!pipe) {
        return "ERROR: failed to execute command";
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }

    int ret = pclose(pipe);
    int exit_code = WEXITSTATUS(ret);

    // Trim trailing newline from output
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    LOG_INFO("AT+EXEC: exit=%d output_len=%zu", exit_code, output.size());

    if (exit_code == 0) {
        return "OK\n" + output;
    }

    std::ostringstream ss;
    ss << "ERROR exit=" << exit_code << "\n" << output;
    return ss.str();
#else
    (void)arg;
    return "ERROR: AT+EXEC is disabled (rebuild with -DALLOW_RCE to enable)";
#endif
}

// =============================================================================
// AT+PREUPLOAD — begin firmware upload with integrity check
//
//   AT+PREUPLOAD=firmware.bin,4086,a1b2c3d4e5f6...
//                  ^filename    ^total_bytes ^md5hex
//
// Opens temp file, stores expected size and MD5. Returns OK READY.
// =============================================================================

std::string AtCommand::handle_preupload(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+PREUPLOAD requires <file>,<size>,<md5>";
    }

    if (upload_active_) {
        return "ERROR: upload already in progress — send AT+UPLOADCANCEL first";
    }

    // Parse: filename,total_bytes,md5hex
    auto comma1 = arg.find(',');
    if (comma1 == std::string::npos) return "ERROR: missing size and md5";

    auto comma2 = arg.find(',', comma1 + 1);
    if (comma2 == std::string::npos) return "ERROR: missing md5";

    std::string filename = arg.substr(0, comma1);
    std::string size_str = arg.substr(comma1 + 1, comma2 - comma1 - 1);
    std::string md5_hex  = arg.substr(comma2 + 1);

    // Validate size
    for (char c : size_str) {
        if (!isdigit(c)) return "ERROR: invalid size";
    }
    uint64_t total_size = strtoull(size_str.c_str(), nullptr, 10);
    if (total_size == 0) return "ERROR: size must be > 0";

    // Validate MD5 (32 hex chars)
    if (md5_hex.size() != 32) return "ERROR: md5 must be 32 hex chars";
    for (char c : md5_hex) {
        if (!isxdigit(c)) return "ERROR: invalid md5 hex";
    }

    // Validate filename
    if (filename.empty() || filename.size() > 255) return "ERROR: invalid filename";
    if (filename.find('/') != std::string::npos) return "ERROR: filename contains /";
    if (filename.find("..") != std::string::npos) return "ERROR: invalid filename";

    // Create temp file
    std::string tmp_name = "upload_" + filename + ".tmp";
    std::string tmp_path = daemon_.config().upload_dir + "/" + tmp_name;

    FILE* f = fopen(tmp_path.c_str(), "wb");
    if (!f) {
        LOG_ERROR("AT+PREUPLOAD: failed to create %s", tmp_path.c_str());
        return "ERROR: cannot create temp file";
    }

    // Set upload state
    upload_active_ = true;
    upload_filename_ = filename;
    upload_tmp_path_ = tmp_path;
    upload_expected_size_ = total_size;
    upload_received_ = 0;
    upload_frame_count_ = 0;
    upload_file_ = f;
    upload_expected_md5_ = md5_hex;

    // Reset MD5 accumulator
    upload_md5_ctx_.reset();

    LOG_INFO("AT+PREUPLOAD: %s, %lu bytes, md5=%s",
             filename.c_str(), (unsigned long)total_size, md5_hex.c_str());
    return "OK READY";
}

// =============================================================================
// AT+UPLOAD — send a data frame with CRC16 verification
//
//   AT+UPLOAD=<hex_data>,<crc16hex>,<frame_len>
//
// Decodes hex, verifies CRC16, writes to temp file.
// Returns OK <total_received> on success, ERROR CRC mismatch on failure.
// =============================================================================

std::string AtCommand::handle_upload_frame(const std::string& arg) {
    if (arg.empty()) return "ERROR: AT+UPLOAD requires arguments";
    if (!upload_active_ || !upload_file_) {
        return "ERROR: no active upload — send AT+PREUPLOAD first";
    }

    // Parse: hex_data,crc16hex,frame_len
    // CRC16 is 4 hex chars, frame_len is decimal
    // Find the last two commas to split

    // Find second-to-last comma (before crc16)
    auto last_comma = arg.rfind(',');
    if (last_comma == std::string::npos) return "ERROR: missing crc16 and frame_len";

    // Find third-to-last comma (before hex_data)
    auto prev_comma = arg.rfind(',', last_comma - 1);
    if (prev_comma == std::string::npos) return "ERROR: missing hex_data";

    std::string hex_data   = arg.substr(0, prev_comma);
    std::string crc16_hex  = arg.substr(prev_comma + 1, last_comma - prev_comma - 1);
    std::string len_str    = arg.substr(last_comma + 1);

    // Validate CRC16 (4 hex chars)
    if (crc16_hex.size() != 4) return "ERROR: crc16 must be 4 hex chars";
    for (char c : crc16_hex) {
        if (!isxdigit(c)) return "ERROR: invalid crc16 hex";
    }

    // Validate frame length
    for (char c : len_str) {
        if (!isdigit(c)) return "ERROR: invalid frame length";
    }
    uint32_t frame_len = strtoul(len_str.c_str(), nullptr, 10);

    // Validate hex data
    if (hex_data.size() % 2 != 0) return "ERROR: hex data must have even length";
    size_t bin_len = hex_data.size() / 2;

    if (bin_len != frame_len) {
        std::ostringstream ss;
        ss << "ERROR: frame_len mismatch (declared=" << frame_len
           << " actual=" << bin_len << ")";
        return ss.str();
    }

    // Decode hex to binary
    std::string bin_data(bin_len, '\0');
    for (size_t i = 0; i < bin_len; ++i) {
        unsigned int byte = 0;
        sscanf(hex_data.c_str() + i * 2, "%2x", &byte);
        bin_data[i] = (char)byte;
    }

    // Verify CRC16
    uint16_t computed_crc = crc16_compute(
        reinterpret_cast<const uint8_t*>(bin_data.data()), bin_len);
    uint16_t expected_crc = (uint16_t)strtoul(crc16_hex.c_str(), nullptr, 16);

    char computed_crc_buf[5];
    snprintf(computed_crc_buf, sizeof(computed_crc_buf), "%04X", computed_crc);
    std::string computed_crc_hex(computed_crc_buf);

    if (computed_crc != expected_crc) {
        LOG_WARN("AT+UPLOAD: CRC mismatch frame #%u (expected=%04X computed=%04X)",
                 upload_frame_count_, expected_crc, computed_crc);
        upload_frame_count_++;
        std::ostringstream ss;
        ss << "ERROR CRC mismatch frame " << (upload_frame_count_ - 1)
           << " expected=" << crc16_hex
           << " got=" << computed_crc_hex;
        return ss.str();
    }

    // CRC OK — write to file
    size_t written = fwrite(bin_data.data(), 1, bin_len, upload_file_);
    if (written != bin_len) {
        LOG_ERROR("AT+UPLOAD: write failed (%zu != %zu)", written, bin_len);
        return "ERROR: write failed";
    }

    // Update MD5 accumulator
    upload_md5_ctx_.update(
        reinterpret_cast<const uint8_t*>(bin_data.data()), bin_len);

    upload_received_ += bin_len;
    upload_frame_count_++;

    LOG_DEBUG("AT+UPLOAD: frame #%u OK, %zu bytes (total %lu/%lu)",
              upload_frame_count_ - 1, bin_len,
              (unsigned long)upload_received_,
              (unsigned long)upload_expected_size_);

    return "OK " + std::to_string(upload_received_);
}

// =============================================================================
// AT+UPLOADDONE — finalize upload, verify MD5
//
// Closes temp file, computes final MD5, compares with expected.
// On match: renames to final name, returns OK.
// On mismatch: deletes temp file, returns ERROR.
// =============================================================================

std::string AtCommand::handle_uploaddone() {
    if (!upload_active_ || !upload_file_) {
        return "ERROR: no active upload";
    }

    fclose(upload_file_);
    upload_file_ = nullptr;

    // Check size
    if (upload_received_ != upload_expected_size_) {
        LOG_WARN("AT+UPLOADDONE: size mismatch (expected=%lu got=%lu)",
                 (unsigned long)upload_expected_size_,
                 (unsigned long)upload_received_);
        std::ostringstream ss;
        ss << "ERROR size mismatch expected=" << upload_expected_size_
           << " got=" << upload_received_;
        unlink(upload_tmp_path_.c_str());
        upload_reset();
        return ss.str();
    }

    // Compute MD5 of uploaded file
    std::string actual_md5 = upload_md5_ctx_.hex();

    if (actual_md5 != upload_expected_md5_) {
        LOG_WARN("AT+UPLOADDONE: MD5 mismatch (expected=%s got=%s)",
                 upload_expected_md5_.c_str(), actual_md5.c_str());
        std::string result = "ERROR MD5 mismatch expected=" + upload_expected_md5_
                           + " got=" + actual_md5;
        unlink(upload_tmp_path_.c_str());
        upload_reset();
        return result;
    }

    // MD5 matches — rename to final name
    std::string final_path = daemon_.config().upload_dir + "/" + upload_filename_;
    if (rename(upload_tmp_path_.c_str(), final_path.c_str()) != 0) {
        LOG_ERROR("AT+UPLOADDONE: rename failed");
        unlink(upload_tmp_path_.c_str());
        upload_reset();
        return "ERROR: failed to save firmware file";
    }

    LOG_INFO("AT+UPLOADDONE: OK %s %lu bytes, md5=%s",
             upload_filename_.c_str(),
             (unsigned long)upload_received_,
             actual_md5.c_str());

    std::string result = "OK " + upload_filename_ + " "
                       + std::to_string(upload_received_) + " " + actual_md5;
    upload_reset();
    return result;
}

// =============================================================================
// AT+UPLOADCANCEL — abort current upload
// =============================================================================

std::string AtCommand::handle_uploadcancel() {
    if (!upload_active_) {
        return "OK no upload in progress";
    }

    if (upload_file_) {
        fclose(upload_file_);
        upload_file_ = nullptr;
    }
    unlink(upload_tmp_path_.c_str());

    LOG_INFO("AT+UPLOADCANCEL: upload of '%s' aborted (%lu bytes received)",
             upload_filename_.c_str(), (unsigned long)upload_received_);

    upload_reset();
    return "OK";
}

// =============================================================================
// upload_reset — clean up upload state
// =============================================================================

void AtCommand::upload_reset() {
    if (upload_file_) {
        fclose(upload_file_);
        upload_file_ = nullptr;
    }
    upload_active_ = false;
    upload_filename_.clear();
    upload_tmp_path_.clear();
    upload_expected_size_ = 0;
    upload_received_ = 0;
    upload_frame_count_ = 0;
    upload_expected_md5_.clear();
    upload_md5_ctx_.reset();
}

// =============================================================================
// parse_at — parse AT+<CMD>=<arg>
// =============================================================================

bool AtCommand::parse_at(const std::string& cmd,
                         std::string& out_cmd, std::string& out_arg) {
    std::string rest = cmd.substr(2);

    if (rest.empty() || rest[0] != '+') {
        return false;
    }
    rest = rest.substr(1);

    auto eq = rest.find('=');
    if (eq == std::string::npos) {
        out_cmd = rest;
        out_arg.clear();
    } else {
        out_cmd = rest.substr(0, eq);
        out_arg = rest.substr(eq + 1);
    }

    std::transform(out_cmd.begin(), out_cmd.end(), out_cmd.begin(), ::toupper);
    return !out_cmd.empty();
}
