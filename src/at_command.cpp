#include "at_command.h"
#include "logger.h"

#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cstring>

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
