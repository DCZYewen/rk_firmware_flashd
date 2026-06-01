#pragma once

#include <string>
#include <cstdint>

struct Config {
    uint16_t port = 8080;
    std::string serial_device = "/dev/ttyUSB0";
    int baud_rate = 115200;
    std::string upload_dir = "/tmp/rk_flashd_uploads";
    std::string log_file;
    std::string pid_file = "/var/run/rk_firmware_flashd.pid";
    bool foreground = false;
    bool help = false;
};

// Parse command-line arguments into Config. Returns false on error.
bool parse_args(int argc, char* argv[], Config& cfg);

// Print usage information.
void print_usage(const char* program);
