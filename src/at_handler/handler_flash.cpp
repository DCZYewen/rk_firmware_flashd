// =============================================================================
// handler_flash.cpp — AT+FLASH
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include <sstream>

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
