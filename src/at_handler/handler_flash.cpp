// =============================================================================
// handler_flash.cpp — AT+FLASH (background thread), AT+TRYFLASHDONE
//
// Model: submit starts a background thread; poll checks status.
//
//   AT+FLASH=<file>[,<mode>]   → start thread, return immediately
//   AT+TRYFLASHDONE             → RUNNING / result / TIMEOUT
//
// Supported modes:
//   FULL     → flash_full.sh
//   PARTIAL  → flash_partial.sh
//   ASSETS   → flash_assets.sh
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include <sstream>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>
#include <exception>

static constexpr long ASYNC_TIMEOUT_SEC = 60;

struct FlashOp {
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> done{false};
    int exit_code = -1;
    std::string output;
    std::string cmd;
    std::chrono::steady_clock::time_point start_time;

    void cancel() {
        if (worker.joinable())
            worker.join();
        running = false;
        done = true;
    }

    long elapsed_sec() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
    }
};

static FlashOp& flash_op() {
    static FlashOp op;
    return op;
}

// ---------------------------------------------------------------------------
// Background worker: popen + read, sets done when finished.
// ---------------------------------------------------------------------------
static void async_flash_worker(const std::string& cmd) {
    try {
        FlashOp& op = flash_op();
        op.running = true;
        op.start_time = std::chrono::steady_clock::now();

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOG_ERROR("async flash: popen failed");
            op.done = true;
            op.running = false;
            return;
        }

        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) {
            op.output += buf;
        }
        while (!op.output.empty() &&
               (op.output.back() == '\n' || op.output.back() == '\r'))
            op.output.pop_back();

        int ret = pclose(pipe);
        op.exit_code = WEXITSTATUS(ret);
        op.done = true;
        op.running = false;

        LOG_INFO("async flash done: exit=%d output='%s'", op.exit_code, op.output.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("async flash worker exception: %s", e.what());
        FlashOp& op = flash_op();
        op.exit_code = -1;
        op.done = true;
        op.running = false;
    }
}

// =============================================================================
// AT+FLASH — submit, start thread, return immediately
// =============================================================================

std::string AtCommand::handle_flash(const std::string& arg) {
    if (arg.empty()) {
        return "ERROR: AT+FLASH requires <file>[,MODE]";
    }

    static const std::map<std::string, std::string> FLASH_SCRIPTS = {
        {"FULL",    "flash_full.sh"},
        {"PARTIAL", "flash_partial.sh"},
        {"ASSETS",  "flash_assets.sh"},
    };

    std::string filename;
    std::string mode = "FULL";

    auto comma = arg.find(',');
    if (comma == std::string::npos) {
        filename = arg;
    } else {
        filename = arg.substr(0, comma);
        mode = arg.substr(comma + 1);
    }

    if (filename.empty()) {
        return "ERROR: AT+FLASH filename cannot be empty";
    }

    auto it = FLASH_SCRIPTS.find(mode);
    if (it == FLASH_SCRIPTS.end()) {
        std::string valid;
        for (auto& kv : FLASH_SCRIPTS) {
            if (!valid.empty()) valid += ", ";
            valid += kv.first;
        }
        return "ERROR: unknown flash mode '" + mode + "' (valid: " + valid + ")";
    }

    FlashOp& op = flash_op();
    if (op.running.load()) {
        return "ERROR: flash already in progress, use AT+TRYFLASHDONE to poll";
    }

    op.cmd = daemon_.config().scripts_dir + "/" + it->second + " " + filename;
    op.done = false;
    op.output.clear();
    op.exit_code = -1;

    LOG_INFO("AT+FLASH [%s]: submitting '%s'", mode.c_str(), op.cmd.c_str());
    op.worker = std::thread(async_flash_worker, op.cmd);

    return "OK submitted: " + op.cmd;
}

// =============================================================================
// AT+TRYFLASHDONE — poll background thread status
// =============================================================================

std::string AtCommand::handle_flashdone() {
    FlashOp& op = flash_op();

    if (!op.running.load() && !op.done.load()) {
        return "ERROR: no pending flash operation";
    }

    if (!op.done.load()) {
        long elapsed = op.elapsed_sec();
        if (elapsed >= ASYNC_TIMEOUT_SEC) {
            LOG_WARN("AT+TRYFLASHDONE: timeout after %lds", elapsed);
            op.cancel();
            return "TIMEOUT: flash did not complete within "
                   + std::to_string(ASYNC_TIMEOUT_SEC) + "s";
        }
        return "RUNNING: " + op.cmd + " (" + std::to_string(elapsed) + "/"
               + std::to_string(ASYNC_TIMEOUT_SEC) + "s)";
    }

    std::string result;
    if (op.exit_code == 0) {
        result = "OK " + op.cmd + " " + op.output;
    } else {
        result = "ERROR " + op.cmd + " failed (exit="
                 + std::to_string(op.exit_code) + ") " + op.output;
    }

    LOG_INFO("AT+TRYFLASHDONE: %s", result.c_str());
    if (op.worker.joinable()) op.worker.join();
    op.running = false;
    op.done = false;
    return result;
}
