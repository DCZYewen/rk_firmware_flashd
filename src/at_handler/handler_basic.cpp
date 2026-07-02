// =============================================================================
// handler_basic.cpp — AT+STATUS, AT+RESET, AT+FORCERESET, AT+HELP
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include "checksum.h"
#include <sstream>
#include <cstdio>

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
        " AT+VERSION               — daemon version"
        " AT+RESET                 — release session, reopen port"
        " AT+FORCERESET            — force-release lock (admin override)"
        " AT+FLASH=f[,FULL|PARTIAL|ASSETS....] — submit flash, returns immediately"
        " AT+TRYFLASHDONE            — poll flash result (60s timeout)"
        " AT+EXEC=cmd              — execute command (ENABLE_RCE only)"
        " AT+REBOOT                — reboot device (ENABLE_REBOOT only)"
        " AT+VERIFY=f              — compute MD5 of file"
        " AT+PREUPLOAD=f,n,md5     — begin upload (size + md5)"
        " AT+UPLOAD=data,crc16,len — send frame with CRC16"
        " AT+UPLOADDONE            — finalize, verify MD5"
        " AT+UPLOADCANCEL          — abort current upload"
        " AT+HELP                  — this help";
}

// =============================================================================
// AT+VERIFY=<file> — compute MD5 of a file
// =============================================================================

std::string AtCommand::handle_verify(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+VERIFY requires a filename";
    }

    if (arg.find("..") != std::string::npos) {
        return "ERROR: invalid filename";
    }

    std::string filepath = daemon_.config().upload_dir + "/" + arg;

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        return "ERROR: cannot open file '" + arg + "'";
    }

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
