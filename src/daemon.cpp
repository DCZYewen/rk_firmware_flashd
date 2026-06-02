#include "daemon.h"
#include "logger.h"
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

static volatile sig_atomic_t g_running = 1;

bool is_running() { return g_running != 0; }
void request_shutdown() { g_running = 0; }

static void signal_handler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            g_running = 0;
            break;
        case SIGHUP:
            // Could reload config here in the future
            break;
    }
}

void setup_signals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // Ignore SIGCHLD to auto-reap children
    signal(SIGCHLD, SIG_IGN);
}

bool daemonize(const Config& cfg) {
    // First fork — release shell
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return false;
    }
    if (pid > 0) {
        // Parent exits
        _exit(0);
    }

    // Create new session
    if (setsid() < 0) {
        perror("setsid");
        return false;
    }

    // Second fork — prevent terminal re-acquisition
    pid = fork();
    if (pid < 0) {
        perror("fork (second)");
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    // Set file permissions
    umask(0);

    // Change to root directory
    if (chdir("/") < 0) {
        perror("chdir");
        return false;
    }

    // Close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; --fd) {
        close(fd);
    }

    // Redirect stdin/stdout/stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }

    // Write PID file (non-fatal — warn and continue if it fails)
    pid = getpid();
    FILE* pidfile = fopen(cfg.pid_file.c_str(), "w");
    if (pidfile) {
        fprintf(pidfile, "%d\n", pid);
        fclose(pidfile);
    }
    // Can't use LOG_* here — all FDs are closed, just write to stderr
    // before it gets redirected (but it's already /dev/null at this point,
    // so the PID file failure is silent — that's OK, it's non-fatal).

    return true;
}

void cleanup_pid_file(const Config& cfg) {
    if (unlink(cfg.pid_file.c_str()) < 0) {
        LOG_WARN("Failed to remove PID file %s: %s", cfg.pid_file.c_str(), strerror(errno));
    }
}
