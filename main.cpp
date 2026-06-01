#include "config.h"
#include "logger.h"
#include "daemon.h"
#include "serial_daemon.h"
#include "at_command.h"
#include "http_server.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

// Recursively create directories (like mkdir -p)
static bool mkdir_p(const std::string& path, mode_t mode = 0755) {
    std::string tmp;
    for (size_t i = 0; i < path.size(); ++i) {
        tmp += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            struct stat st;
            if (stat(tmp.c_str(), &st) != 0) {
                if (mkdir(tmp.c_str(), mode) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    // Parse configuration
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        fprintf(stderr, "Use --help for usage information.\n");
        return 1;
    }
    if (cfg.help) {
        print_usage(argv[0]);
        return 0;
    }

    // Create upload directory if it doesn't exist
    if (!mkdir_p(cfg.upload_dir)) {
        fprintf(stderr, "Warning: failed to create upload dir '%s'\n",
                cfg.upload_dir.c_str());
    }

    // Initialize logger (stderr output for foreground, syslog for daemon)
    Logger::instance().init(cfg.log_file, !cfg.foreground);

    LOG_INFO("RK Firmware Flash Daemon starting...");

    // Daemonize (unless --foreground)
    if (!cfg.foreground) {
        if (!daemonize(cfg)) {
            LOG_ERROR("Failed to daemonize");
            return 1;
        }
    } else {
        LOG_INFO("Running in foreground mode");
    }

    // Set up signal handlers
    setup_signals();

    // Start serial daemon (manages serial port via state machine)
    SerialDaemon serial_daemon(cfg);
    serial_daemon.start();

    // Create unified AT command processor (shared by HTTP and serial)
    AtCommand at_cmd(serial_daemon);

    // Start serial reader thread — reads AT commands from the device
    // and processes them through the same AtCommand processor.
    if (cfg.use_serial()) {
        serial_daemon.start_reader([&at_cmd](const std::string& line) {
            return at_cmd.process(line, SerialDaemon::Owner::SERIAL);
        });
    }

    // Start HTTP server
    HttpServer http_server(cfg, serial_daemon);
    if (!http_server.start()) {
        LOG_ERROR("Failed to start HTTP server");
        serial_daemon.stop();
        if (!cfg.foreground) cleanup_pid_file(cfg);
        return 1;
    }

    LOG_INFO("Daemon ready. Listening on port %d", cfg.port);

    // Main loop — wait for shutdown signal
    while (is_running()) {
        sleep(1);
    }

    // Cleanup
    LOG_INFO("Shutting down...");
    http_server.stop();
    serial_daemon.stop_reader();
    serial_daemon.stop();
    if (!cfg.foreground) cleanup_pid_file(cfg);
    Logger::instance().shutdown();

    return 0;
}
