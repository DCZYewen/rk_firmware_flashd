#include "http_server.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <chrono>

using json = nlohmann::json;

MHD_Result handleStatus(void* cls, MHD_Connection* connection,
                        const char* method) {
    HttpServer* server = static_cast<HttpServer*>(cls);
    (void)method;

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - server->start_time()).count();

    json resp;
    resp["status"] = "ok";
    resp["version"] = "0.1.0";
    resp["uptime_seconds"] = uptime;
    resp["serial_port"] = server->config().serial_device;
    resp["serial_connected"] = server->daemon().is_open();
    resp["daemon_busy"] = server->daemon().is_busy();

    // Lock owner
    if (server->daemon().is_busy()) {
        resp["lock_owner"] = (server->daemon().owner() == SerialDaemon::Owner::HTTP) ? "HTTP" : "SERIAL";
    } else {
        resp["lock_owner"] = nullptr;
    }

    resp["port"] = server->config().port;
    resp["upload_dir"] = server->config().upload_dir;

    return HttpServer::sendJson(connection, 200, resp.dump());
}
