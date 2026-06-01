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

    // Must start with "AT"
    if (cmd.size() < 2 || cmd[0] != 'A' || cmd[1] != 'T') {
        return "ERROR: commands must start with AT";
    }

    std::string action, arg;
    if (!parse_at(cmd, action, arg)) {
        return "ERROR: invalid AT command format";
    }

    // --- All commands handled locally by the daemon ---
    if (action == "STATUS")     return handle_status();
    if (action == "RESET")      return handle_reset(caller);
    if (action == "FORCERESET") return handle_forcereset();
    if (action == "HELP")       return handle_help();
    if (action == "FLASH")      return handle_flash(arg);
    if (action == "UPLOAD")     return handle_upload(arg);

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
    // Only the lock owner can release.
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
        " AT+STATUS        — daemon status"
        " AT+RESET         — release session, reopen port"
        " AT+FORCERESET    — force-release lock (admin override)"
        " AT+FLASH=f       — execute /sbin/flash.sh <f>"
        " AT+UPLOAD=f,n    — begin serial upload (n bytes)"
        " AT+UPLOAD=f,off,data — store firmware slice"
        " AT+UPLOAD=f,END  — finish upload, write to disk"
        " AT+HELP          — this help";
}

// =============================================================================
// AT+FLASH — execute /sbin/flash.sh
// =============================================================================

std::string AtCommand::handle_flash(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+FLASH requires a filename";
    }

    std::string script = "/sbin/flash.sh " + arg;
    LOG_INFO("AT+FLASH: executing '%s'", script.c_str());

    int ret = std::system(script.c_str());
    if (ret == 0) {
        return "OK flash " + arg;
    }

    std::ostringstream ss;
    ss << "ERROR flash failed (exit code " << ret << ")";
    return ss.str();
}

// =============================================================================
// AT+UPLOAD — serial firmware upload (stores data locally)
// =============================================================================

std::string AtCommand::handle_upload(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+UPLOAD requires arguments";
    }

    // Parse first field: filename
    auto comma1 = arg.find(',');
    if (comma1 == std::string::npos) {
        return "ERROR: AT+UPLOAD requires <file>,<action>";
    }

    std::string filename = arg.substr(0, comma1);
    std::string rest = arg.substr(comma1 + 1);

    // --- AT+UPLOAD=<file>,END ---
    if (rest == "END") {
        if (!upload_active_ || !upload_file_) {
            return "ERROR: no active upload to end";
        }

        fclose(upload_file_);
        upload_file_ = nullptr;

        // Build final path with timestamp
        time_t now = time(nullptr);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", localtime(&now));

        std::string final_name = std::string(time_str) + "_" + filename;
        std::string final_path = daemon_.config().upload_dir + "/" + final_name;

        if (rename(upload_tmp_path_.c_str(), final_path.c_str()) != 0) {
            LOG_ERROR("AT+UPLOAD END: failed to rename %s -> %s",
                      upload_tmp_path_.c_str(), final_path.c_str());
            unlink(upload_tmp_path_.c_str());
            upload_reset();
            return "ERROR: failed to save firmware file";
        }

        LOG_INFO("AT+UPLOAD END: saved %s (%lu bytes)",
                 final_name.c_str(), (unsigned long)upload_received_);
        std::string result = "OK " + final_name + " " + std::to_string(upload_received_);
        upload_reset();
        return result;
    }

    // --- AT+UPLOAD=<file>,BEGIN  or  AT+UPLOAD=<file>,<size> ---
    if (rest == "BEGIN") {
        // Explicit BEGIN
        if (upload_active_) {
            return "ERROR: upload already in progress";
        }

        // Generate temp file path
        std::string tmp_name = "upload_" + filename + ".tmp";
        upload_tmp_path_ = daemon_.config().upload_dir + "/" + tmp_name;

        upload_file_ = fopen(upload_tmp_path_.c_str(), "wb");
        if (!upload_file_) {
            LOG_ERROR("AT+UPLOAD BEGIN: failed to create %s", upload_tmp_path_.c_str());
            return "ERROR: cannot create temp file";
        }

        upload_active_ = true;
        upload_filename_ = filename;
        upload_expected_size_ = 0;
        upload_received_ = 0;

        LOG_INFO("AT+UPLOAD BEGIN: %s (streaming mode)", filename.c_str());
        return "OK READY";
    }

    // Check if rest is a number (size) → start upload with known size
    bool all_digits = true;
    for (char c : rest) {
        if (!isdigit(c)) { all_digits = false; break; }
    }

    if (all_digits && !rest.empty()) {
        if (upload_active_) {
            return "ERROR: upload already in progress";
        }

        upload_expected_size_ = strtoull(rest.c_str(), nullptr, 10);

        std::string tmp_name = "upload_" + filename + ".tmp";
        upload_tmp_path_ = daemon_.config().upload_dir + "/" + tmp_name;

        upload_file_ = fopen(upload_tmp_path_.c_str(), "wb");
        if (!upload_file_) {
            LOG_ERROR("AT+UPLOAD: failed to create %s", upload_tmp_path_.c_str());
            return "ERROR: cannot create temp file";
        }

        upload_active_ = true;
        upload_filename_ = filename;
        upload_received_ = 0;

        LOG_INFO("AT+UPLOAD: %s expecting %lu bytes",
                 filename.c_str(), (unsigned long)upload_expected_size_);
        return "OK READY";
    }

    // --- AT+UPLOAD=<file>,<offset>,<data> ---
    // rest contains "<offset>,<hex_data>"
    if (!upload_active_ || !upload_file_) {
        return "ERROR: no active upload — send BEGIN or <size> first";
    }
    if (filename != upload_filename_) {
        return "ERROR: filename mismatch with active upload";
    }

    auto comma2 = rest.find(',');
    if (comma2 == std::string::npos) {
        return "ERROR: AT+UPLOAD data requires <offset>,<hex_data>";
    }

    std::string offset_str = rest.substr(0, comma2);
    std::string hex_data = rest.substr(comma2 + 1);

    // Validate offset
    bool offset_ok = true;
    for (char c : offset_str) {
        if (!isdigit(c)) { offset_ok = false; break; }
    }
    if (!offset_ok || offset_str.empty()) {
        return "ERROR: invalid offset";
    }
    uint64_t offset = strtoull(offset_str.c_str(), nullptr, 10);

    // Validate hex data (must be even length, all hex chars)
    if (hex_data.size() % 2 != 0) {
        return "ERROR: hex data must have even length";
    }
    for (char c : hex_data) {
        if (!isxdigit(c)) {
            return "ERROR: invalid hex data";
        }
    }

    // Seek to offset and write decoded data
    if (fseeko(upload_file_, (off_t)offset, SEEK_SET) != 0) {
        LOG_ERROR("AT+UPLOAD: fseeko failed for offset %lu", (unsigned long)offset);
        return "ERROR: seek failed";
    }

    // Decode hex to binary
    size_t bin_len = hex_data.size() / 2;
    std::string bin_data(bin_len, '\0');
    for (size_t i = 0; i < bin_len; ++i) {
        unsigned int byte = 0;
        sscanf(hex_data.c_str() + i * 2, "%2x", &byte);
        bin_data[i] = (char)byte;
    }

    size_t written = fwrite(bin_data.data(), 1, bin_len, upload_file_);
    if (written != bin_len) {
        LOG_ERROR("AT+UPLOAD: write failed (%zu != %zu)", written, bin_len);
        return "ERROR: write failed";
    }

    upload_received_ += bin_len;

    LOG_DEBUG("AT+UPLOAD: wrote %zu bytes at offset %lu (total %lu)",
              bin_len, (unsigned long)offset, (unsigned long)upload_received_);
    return "OK " + std::to_string(upload_received_);
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
}

// =============================================================================
// parse_at — parse AT+<CMD>=<arg>
// =============================================================================

bool AtCommand::parse_at(const std::string& cmd,
                         std::string& out_cmd, std::string& out_arg) {
    // Skip "AT" prefix
    std::string rest = cmd.substr(2);

    // AT alone (no +) — invalid
    if (rest.empty() || rest[0] != '+') {
        return false;
    }
    rest = rest.substr(1); // skip '+'

    // Split on '='
    auto eq = rest.find('=');
    if (eq == std::string::npos) {
        out_cmd = rest;
        out_arg.clear();
    } else {
        out_cmd = rest.substr(0, eq);
        out_arg = rest.substr(eq + 1);
    }

    // Uppercase the command name
    std::transform(out_cmd.begin(), out_cmd.end(), out_cmd.begin(), ::toupper);

    return !out_cmd.empty();
}
