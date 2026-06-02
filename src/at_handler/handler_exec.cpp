// =============================================================================
// handler_exec.cpp — AT+EXEC (requires ALLOW_RCE)
// =============================================================================

#include "at_command.h"
#include "logger.h"
#include <cstdio>
#include <sstream>

std::string AtCommand::handle_exec(const std::string& arg) {
#ifdef ALLOW_RCE
    if (arg.empty()) {
        return "ERROR: AT+EXEC requires a command";
    }

    LOG_WARN("AT+EXEC: executing '%s'", arg.c_str());

    FILE* pipe = popen(arg.c_str(), "r");
    if (!pipe) {
        return "ERROR: failed to execute command";
    }

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }

    int ret = pclose(pipe);
    int exit_code = WEXITSTATUS(ret);

    // Trim trailing newline from output
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    LOG_INFO("AT+EXEC: exit=%d output_len=%zu", exit_code, output.size());

    if (exit_code == 0) {
        return "OK\n" + output;
    }

    std::ostringstream ss;
    ss << "ERROR exit=" << exit_code << "\n" << output;
    return ss.str();
#else
    (void)arg;
    return "ERROR: AT+EXEC is disabled (rebuild with -DALLOW_RCE to enable)";
#endif
}
