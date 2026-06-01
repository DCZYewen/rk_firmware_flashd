#pragma once

// =============================================================================
// AtCommand — Unified AT command processor.
//
// Both HTTP and serial feed into the same processor.  All commands are
// handled locally by the daemon — nothing is relayed to the device.
//
// Command set:
//   AT+STATUS          → daemon status (uptime, serial state, etc.)
//   AT+RESET           → release session lock, reopen serial port
//   AT+FLASH=<file>    → execute /sbin/flash.sh <file>
//   AT+UPLOAD=<file>,<size>
//                     → begin storing firmware (serial upload protocol)
//   AT+UPLOAD=<file>,<offset>,<data>
//                     → store a firmware slice
//   AT+UPLOAD=<file>,END
//                     → finish upload, write to disk
//   AT+FORCERESET      → force-release session lock (admin override)
//   AT+HELP            → list available commands
//   anything else      → ERROR: unknown command
//
// Serial upload protocol (the "stupid" way):
//   1. Client sends:  AT+UPLOAD=fw.bin,102400
//   2. Daemon prepares to receive 102400 bytes
//   3. Client sends slices:  AT+UPLOAD=fw.bin,0,<1024 bytes binary>
//                            AT+UPLOAD=fw.bin,1024,<1024 bytes binary>
//                            ...
//   4. Client sends:  AT+UPLOAD=fw.bin,END
//   5. Daemon writes complete file to disk, responds OK
// =============================================================================

#include "serial_daemon.h"

#include <cstdint>
#include <string>
#include <chrono>

class AtCommand {
public:
    explicit AtCommand(SerialDaemon& daemon);

    // Process an AT command from a given source.
    // caller indicates who is invoking (HTTP or SERIAL).
    // AT+RESET only works if caller matches the lock owner.
    std::string process(const std::string& raw_cmd,
                        SerialDaemon::Owner caller,
                        int timeout_ms = 5000);

private:
    // Local commands (processed by the daemon itself)
    std::string handle_status();
    std::string handle_reset(SerialDaemon::Owner caller);
    std::string handle_help();
    std::string handle_flash(const std::string& arg);
    std::string handle_upload(const std::string& arg);
    std::string handle_forcereset();

    // Upload state (for serial upload protocol)
    bool upload_active_ = false;
    std::string upload_filename_;
    std::string upload_tmp_path_;
    uint64_t upload_expected_size_ = 0;
    uint64_t upload_received_ = 0;
    FILE* upload_file_ = nullptr;

    void upload_reset();

    // Parse AT+<CMD>=<arg> → ("CMD", "arg").  If no '=', arg is empty.
    static bool parse_at(const std::string& cmd,
                         std::string& out_cmd, std::string& out_arg);

    SerialDaemon& daemon_;
    std::chrono::steady_clock::time_point start_time_;
};
