#include "http_server.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <sys/stat.h>

using json = nlohmann::json;

MHD_Result handleUpload(void* cls, MHD_Connection* connection,
                        const char* method, const char* url,
                        const char* upload_data, size_t* upload_data_size,
                        ConnectionState* state, bool is_first_call) {
    HttpServer* server = static_cast<HttpServer*>(cls);
    (void)method; (void)url;

    // First call — set up POST processor (already done in accessHandler)
    if (is_first_call) {
        if (!state->pp) {
            return HttpServer::sendJson(connection, 400,
                R"({"status":"error","message":"No multipart data"})");
        }
        return MHD_YES;
    }

    // Feed data to POST processor
    if (*upload_data_size > 0) {
        MHD_post_process(state->pp, upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    // All data received — finalize
    if (state->upload_file) {
        fclose(state->upload_file);
        state->upload_file = nullptr;

        // Move to final location with timestamp
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::system_clock::to_time_t(now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", localtime(&ts));

        std::string final_name = std::string(time_str) + "_" + state->upload_filename;
        std::string final_path = server->config().upload_dir + "/" + final_name;

        if (access(state->upload_tmp_path.c_str(), F_OK) == 0) {
            if (rename(state->upload_tmp_path.c_str(), final_path.c_str()) != 0) {
                LOG_ERROR("Failed to rename upload file: %s -> %s",
                          state->upload_tmp_path.c_str(), final_path.c_str());
                return HttpServer::sendJson(connection, 500,
                    R"({"status":"error","message":"Failed to save file"})");
            }
        }

        json resp;
        resp["status"] = "ok";
        resp["filename"] = final_name;
        resp["size"] = state->upload_size;

        LOG_INFO("File uploaded: %s (%lu bytes)", final_name.c_str(), (unsigned long)state->upload_size);
        return HttpServer::sendJson(connection, 200, resp.dump());
    }

    return HttpServer::sendJson(connection, 400,
        R"({"status":"error","message":"Upload failed"})");
}
