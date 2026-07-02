#pragma once

// =============================================================================
// AtCommand — Unified AT command processor.
//
// Both HTTP and serial feed into the same processor.  All commands are
// handled locally by the daemon — nothing is relayed to the device.
//
// Command set:
//   AT+STATUS               → daemon status
//   AT+RESET                → release session lock
//   AT+FORCERESET           → force-release session lock (admin override)
//   AT+FLASH=<file>         → execute /sbin/flash.sh <file>
//   AT+HELP                 → list available commands
//
// Firmware upload protocol (with integrity verification):
//   Phase 1 — Handshake:
//     AT+PREUPLOAD=<file>,<total_bytes>,<md5hex>
//     Daemon → OK READY
//
//   Phase 2 — Data frames:
//     AT+UPLOAD=<hex_data>,<crc16hex>,<frame_len>
//     Daemon → OK <bytes_received>   (CRC matches)
//     Daemon → ERROR CRC mismatch   (CRC fails, client should resend frame)
//
//   Phase 3 — Finalize:
//     AT+UPLOADDONE
//     Daemon → OK <file> <size>      (MD5 matches)
//     Daemon → ERROR MD5 mismatch    (MD5 fails)
//
//   Abort at any time:
//     AT+UPLOADCANCEL
//     Daemon → OK (upload state cleared)
// =============================================================================

#include "serial_daemon.h"
#include "checksum.h"

#include <cstdint>
#include <string>
#include <chrono>

class AtCommand {
public:
    explicit AtCommand(SerialDaemon& daemon);

    // Process an AT command from a given source.
    std::string process(const std::string& raw_cmd,
                        SerialDaemon::Owner caller,
                        int timeout_ms = 5000);

private:
    // Command handlers
    std::string handle_status();
    std::string handle_version();
    std::string handle_reset(SerialDaemon::Owner caller);
    std::string handle_help();
    std::string handle_flash(const std::string& arg);
    std::string handle_flashdone();
    std::string handle_exec(const std::string& arg);
    std::string handle_forcereset();
    std::string handle_reboot();
    std::string handle_verify(const std::string& arg);

    // Upload protocol handlers
    std::string handle_preupload(const std::string& arg);
    std::string handle_upload_frame(const std::string& arg);
    std::string handle_uploaddone();
    std::string handle_uploadcancel();

    // Parse AT+<CMD>=<arg> → ("CMD", "arg").  If no '=', arg is empty.
    static bool parse_at(const std::string& cmd,
                         std::string& out_cmd, std::string& out_arg);

    SerialDaemon& daemon_;
    std::chrono::steady_clock::time_point start_time_;
};
