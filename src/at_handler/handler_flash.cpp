// =============================================================================
// handler_flash.cpp — AT+FLASH (deferred-exec), AT+TRYFLASHDONE
//
// Model: submit stores the command; first poll actually runs it.
// No background threads — the poll is synchronous but the caller
// sees the same "submit → poll" pattern.
//
//   AT+FLASH=<file>[,<mode>]   → store cmd, return immediately
//   AT+TRYFLASHDONE             → run on first call, return result
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
#include <chrono>

static constexpr long ASYNC_TIMEOUT_SEC = 60;

struct FlashOp {
    bool pending = false;
    bool done = false;
    int exit_code = -1;
    std::string output;
    std::string cmd;
    std::chrono::steady_clock::time_point start_time;

    void reset() {
        pending = false;
        done = false;
        exit_code = -1;
        output.clear();
        cmd.clear();
    }
};

static FlashOp& flash_op() {
    static FlashOp op;
    return op;
}

// Run the command via popen, fill exit_code + output, mark done.
static void run_flash(FlashOp& op) {
    op.start_time = std::chrono::steady_clock::now();

    FILE* pipe = popen(op.cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERROR("flash: popen failed");
        op.exit_code = -1;
        op.done = true;
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

    LOG_INFO("flash done: exit=%d output='%s'", op.exit_code, op.output.c_str());
}

// =============================================================================
// AT+FLASH — validate args, store cmd, return immediately
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
    if (op.pending || op.done) {
        return "ERROR: flash already in progress, use AT+TRYFLASHDONE to poll";
    }

    op.cmd = daemon_.config().scripts_dir + "/" + it->second + " " + filename;
    op.pending = true;
    op.done = false;
    op.output.clear();
    op.exit_code = -1;

    LOG_INFO("AT+FLASH [%s]: submitted '%s'", mode.c_str(), op.cmd.c_str());
    return "OK submitted: " + op.cmd;
}

// =============================================================================
// AT+TRYFLASHDONE — run on first call, poll thereafter
// =============================================================================

std::string AtCommand::handle_flashdone() {
    FlashOp& op = flash_op();

    if (!op.pending && !op.done) {
        return "ERROR: no pending flash operation";
    }

    // First poll — actually run the command now
    if (op.pending && !op.done) {
        run_flash(op);
        op.pending = false;
    }

    // Done — return result
    std::string result;
    if (op.exit_code == 0) {
        result = "OK " + op.cmd + " " + op.output;
    } else {
        result = "ERROR " + op.cmd + " failed (exit="
                 + std::to_string(op.exit_code) + ") " + op.output;
    }

    LOG_INFO("AT+TRYFLASHDONE: %s", result.c_str());
    op.reset();
    return result;
}
