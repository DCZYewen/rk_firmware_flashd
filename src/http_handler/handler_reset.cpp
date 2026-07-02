#include "http_server.h"
#include "logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

MHD_Result handleReset(void* cls, MHD_Connection* connection,
                       const char* method) {
    HttpServer* server = static_cast<HttpServer*>(cls);
    (void)method;

    LOG_INFO("HTTP /api/reset: force-releasing lock");
    server->daemon().force_release();

    json resp;
    resp["status"] = "ok";
    return HttpServer::sendJson(connection, 200, resp.dump());
}
