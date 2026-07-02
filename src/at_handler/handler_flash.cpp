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

std::string AtCommand::handle_flash(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+FLASH requires <file>[,MODE]";
    }

    // Mode name → script path mapping (function-local for safe static init order)
    static const std::map<std::string, std::string> FLASH_SCRIPTS = {
        {"FULL",    "/sbin/flash_full.sh"},
        {"PARTIAL", "/sbin/flash_partial.sh"},
        {"ASSETS",  "/sbin/flash_assets.sh"},
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
        // Build error with list of valid modes
        std::string valid;
        for (auto& kv : FLASH_SCRIPTS) {
            if (!valid.empty()) valid += ", ";
            valid += kv.first;
        }
        return "ERROR: unknown flash mode '" + mode + "' (valid: " + valid + ")";
    }

    const std::string& script = it->second;
    std::string cmd = script + " " + filename;
    LOG_INFO("AT+FLASH [%s]: executing '%s'", mode.c_str(), cmd.c_str());

    int ret = std::system(cmd.c_str());
    if (ret == 0) {
        return "OK flash " + mode + " " + filename;
    }

    std::ostringstream ss;
    ss << "ERROR flash failed (exit code " << ret << ")";
    return ss.str();
}
