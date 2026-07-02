// =============================================================================
// handler_flash.cpp — AT+FLASH
//
//   AT+FLASH=<file>              → flash with default mode (FULL)
//   AT+FLASH=<file>,<mode>       → flash with specified mode
//
// Supported modes (extend the map to add more):
//   FULL     → /sbin/flash_full.sh
//   PARTIAL  → /sbin/flash_partial.sh
//   ASSETS   → /sbin/flash_assets.sh
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include <sstream>
#include <map>
#include <cstdio>

std::string AtCommand::handle_flash(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+FLASH requires <file>[,MODE]";
    }

    // Mode name → script name mapping
    static const std::map<std::string, std::string> FLASH_SCRIPTS = {
        {"FULL",    "flash_full.sh"},
        {"PARTIAL", "flash_partial.sh"},
        {"ASSETS",  "flash_assets.sh"},
    };

    // Parse: file[,mode]
    std::string filename;
    std::string mode = "FULL";

    auto comma = arg.find(',');
    if (comma == std::string::npos) {
        filename = arg;
    } else {
        filename = arg.substr(0, comma);
        mode = arg.substr(comma + 1);
    }

    if (filename.empty()) {
        return "ERROR: AT+FLASH filename cannot be empty";
    }

    // Look up mode in the map
    auto it = FLASH_SCRIPTS.find(mode);
    if (it == FLASH_SCRIPTS.end()) {
        std::string valid;
        for (auto& kv : FLASH_SCRIPTS) {
            if (!valid.empty()) valid += ", ";
            valid += kv.first;
        }
        return "ERROR: unknown flash mode '" + mode + "' (valid: " + valid + ")";
    }

    std::string cmd = daemon_.config().scripts_dir + "/" + it->second + " " + filename;
    LOG_INFO("AT+FLASH [%s]: executing '%s'", mode.c_str(), cmd.c_str());

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERROR("AT+FLASH: popen failed");
        return "ERROR: failed to execute flash script";
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    int ret = pclose(pipe);
    int exit_code = WEXITSTATUS(ret);

    LOG_INFO("AT+FLASH [%s]: exit=%d output='%s'", mode.c_str(), exit_code, output.c_str());

    if (exit_code == 0) {
        return "OK " + cmd + " " + output;
    }

    std::ostringstream ss;
    ss << "ERROR " << cmd << " failed (exit=" << exit_code << ") " << output;
    return ss.str();
}
