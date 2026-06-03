// =============================================================================
// handler_version.cpp — AT+VERSION, AT+REBOOT
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include <sstream>
#include <cstdlib>
#include <unistd.h>

// Version defined at build time, fallback to dev version
#ifndef RK_FIRMWARE_VERSION
#define RK_FIRMWARE_VERSION "0.1.0-dev"
#endif

// =============================================================================
// AT+VERSION — return daemon and system info
// =============================================================================

std::string AtCommand::handle_version() {
    std::ostringstream ss;
    ss << "OK"
       << " daemon=" << RK_FIRMWARE_VERSION
       << " arch=";
#if defined(__x86_64__)
    ss << "x86_64";
#elif defined(__aarch64__)
    ss << "aarch64";
#elif defined(__arm__)
    ss << "arm";
#else
    ss << "unknown";
#endif
    ss << " build=" __DATE__ " " __TIME__;
    return ss.str();
}

// =============================================================================
// AT+REBOOT — reboot the device
// =============================================================================

std::string AtCommand::handle_reboot() {
#ifdef ALLOW_REBOOT
    LOG_WARN("AT+REBOOT: system reboot requested");

    // Sync filesystems before reboot
    sync();

    int ret = std::system("reboot");
    if (ret == 0) {
        return "OK rebooting";
    }

    // If reboot command fails, try shutdown -r now
    ret = std::system("shutdown -r now");
    if (ret == 0) {
        return "OK rebooting";
    }

    return "ERROR: reboot failed (exit code " + std::to_string(ret) + ")";
#else
    return "ERROR: AT+REBOOT is disabled (rebuild with -DENABLE_REBOOT=ON)";
#endif
}
