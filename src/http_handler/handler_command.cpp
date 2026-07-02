#include "http_server.h"
#include "at_command.h"
#include "logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

MHD_Result handleCommand(void* cls, MHD_Connection* connection,
                         const char* method, const char* url,
                         const char* upload_data, size_t* upload_data_size,
                         ConnectionState* state, bool is_first_call) {
    HttpServer* server = static_cast<HttpServer*>(cls);
    (void)method;
    (void)url;
    (void)is_first_call;

    // Accumulate body data as it arrives
    if (*upload_data_size > 0) {
        state->cmd_body.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    // No data yet — keep waiting for body to arrive
    if (state->cmd_body.empty()) {
        return MHD_YES;
    }

    // All data received — process
    try {
        auto j = json::parse(state->cmd_body);

        if (!j.contains("cmd") || !j["cmd"].is_string()) {
            return HttpServer::sendJson(connection, 400,
                R"({"status":"error","message":"Missing 'cmd' field"})");
        }

        std::string cmd = j["cmd"];

        // Extract AT command action for special handling.
        // Commands that are safe regardless of lock owner bypass the session check.
        bool bypass_lock = false;
        if (cmd.size() > 4 && cmd[0] == 'A' && cmd[1] == 'T' && cmd[2] == '+') {
            std::string action;
            auto eq = cmd.find('=', 3);
            action = cmd.substr(3, eq == std::string::npos ? eq : eq - 3);
            // FORCERESET always works. RESET should reach the processor so
            // AtCommand can return the proper "only owner can reset" error.
            if (action == "FORCERESET" || action == "RESET")
                bypass_lock = true;
        }

        if (!bypass_lock) {
            // Session-based lock: HTTP holds lock across multiple requests until AT+RESET.
            if (!server->daemon().is_busy()) {
                // Nobody holds the lock — try to acquire for HTTP.
                if (!server->daemon().try_acquire(SerialDaemon::Owner::HTTP)) {
                    json resp;
                    resp["status"] = "busy";
                    resp["message"] = "serial daemon is busy, wait for AT+RESET";
                    return HttpServer::sendJson(connection, 503, resp.dump());
                }
            } else if (server->daemon().owner() != SerialDaemon::Owner::HTTP) {
                // Lock held by SERIAL — busy.
                json resp;
                resp["status"] = "busy";
                resp["message"] = "serial daemon is busy, wait for AT+RESET";
                return HttpServer::sendJson(connection, 503, resp.dump());
            }
            // else: HTTP already owns the lock — continue session.
        }

        // Process the command under the lock.
        AtCommand at_cmd(server->daemon());
        std::string response = at_cmd.process(cmd, SerialDaemon::Owner::HTTP);

        json resp;
        resp["status"] = "ok";
        resp["response"] = response;

        LOG_INFO("HTTP cmd='%s' → '%s'", cmd.c_str(), response.c_str());
        return HttpServer::sendJson(connection, 200, resp.dump());

    } catch (const json::parse_error& e) {
        json err;
        err["status"] = "error";
        err["message"] = std::string("Invalid JSON: ") + e.what();
        return HttpServer::sendJson(connection, 400, err.dump());
    }
}
