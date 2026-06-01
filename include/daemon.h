#pragma once

#include "config.h"

// Daemonize the process. Writes PID file.
// Returns true on success, false on error.
bool daemonize(const Config& cfg);

// Set up signal handlers (SIGTERM, SIGINT, SIGHUP).
void setup_signals();

// Check if daemon is still running (g_running flag).
bool is_running();

// Request graceful shutdown.
void request_shutdown();

// Remove PID file.
void cleanup_pid_file(const Config& cfg);
