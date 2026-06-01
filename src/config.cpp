#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

void print_usage(const char* program) {
    printf(
        "Usage: %s [OPTIONS]\n"
        "RK Firmware Flash Daemon\n"
        "\n"
        "Options:\n"
        "  --port <port>           HTTP listen port (default: 8080)\n"
        "  --serial-device <path>  Serial device path (optional, disabled if omitted)\n"
        "  --baud-rate <rate>      Serial baud rate (default: 115200)\n"
        "  --upload-dir <dir>      Upload directory (default: /tmp/rk_flashd_uploads)\n"
        "  --log-file <path>       Log file path (optional, logs to syslog + stderr)\n"
        "  --pid-file <path>       PID file path (default: /var/run/rk_firmware_flashd.pid)\n"
        "  --foreground            Run in foreground (do not daemonize)\n"
        "  --help                  Show this help message\n"
        "\n"
        "Examples:\n"
        "  %s --foreground\n"
        "  %s --port 9090 --serial-device /dev/ttyS1 --foreground\n",
        program, program, program
    );
}

bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            cfg.help = true;
            return true;
        } else if (strcmp(arg, "--foreground") == 0) {
            cfg.foreground = true;
        } else if (i + 1 < argc) {
            if (strcmp(arg, "--port") == 0) {
                cfg.port = static_cast<uint16_t>(atoi(argv[++i]));
                if (cfg.port == 0) {
                    fprintf(stderr, "Error: invalid port %s\n", argv[i]);
                    return false;
                }
            } else if (strcmp(arg, "--serial-device") == 0) {
                cfg.serial_device = argv[++i];
            } else if (strcmp(arg, "--baud-rate") == 0) {
                cfg.baud_rate = atoi(argv[++i]);
                if (cfg.baud_rate <= 0) {
                    fprintf(stderr, "Error: invalid baud rate %s\n", argv[i]);
                    return false;
                }
            } else if (strcmp(arg, "--upload-dir") == 0) {
                cfg.upload_dir = argv[++i];
            } else if (strcmp(arg, "--log-file") == 0) {
                cfg.log_file = argv[++i];
            } else if (strcmp(arg, "--pid-file") == 0) {
                cfg.pid_file = argv[++i];
            } else {
                fprintf(stderr, "Error: unknown option '%s'\n", arg);
                return false;
            }
        } else {
            fprintf(stderr, "Error: option '%s' requires a value\n", arg);
            return false;
        }
    }
    return true;
}
