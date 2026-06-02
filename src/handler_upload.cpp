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

    LOG_DEBUG("handleUpload: first_call=%d data_size=%zu pp=%p file=%p",
              is_first_call, *upload_data_size,
              (void*)state->pp, (void*)state->upload_file);

    // No post processor — can't handle this request
    if (!state->pp) {
        LOG_WARN("handleUpload: no post processor — not multipart?");
        return HttpServer::sendJson(connection, 400,
            R"({"status":"error","message":"No multipart data"})");
    }

    // Feed data to POST processor as it arrives
    if (*upload_data_size > 0) {
        LOG_DEBUG("handleUpload: feeding %zu bytes to post processor", *upload_data_size);
        MHD_post_process(state->pp, upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    // No data this call — check if we've received anything yet
    if (state->upload_size == 0 && !state->upload_file) {
        // Initial call, nothing received yet — keep waiting
        return MHD_YES;
    }

    // All data received — finalize
    if (state->upload_file) {
        fclose(state->upload_file);
        state->upload_file = nullptr;

        // Validate filename before final save
        if (!server->validate_filename()(state->upload_filename)) {
            LOG_WARN("Upload rejected: invalid filename '%s'", state->upload_filename.c_str());
            unlink(state->upload_tmp_path.c_str());
            json resp;
            resp["status"] = "error";
            resp["message"] = "invalid filename";
            return HttpServer::sendJson(connection, 400, resp.dump());
        }

        // Use the original filename as-is
        std::string final_name = state->upload_filename;
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
        resp["path"] = final_path;
        resp["size"] = state->upload_size;

        LOG_INFO("File uploaded: %s (%lu bytes)", final_name.c_str(), (unsigned long)state->upload_size);
        return HttpServer::sendJson(connection, 200, resp.dump());
    }

    // No file was opened — filename was likely rejected by validator
    if (state->upload_size > 0 || !state->upload_filename.empty()) {
        LOG_WARN("Upload completed but no file created for '%s'", state->upload_filename.c_str());
        return HttpServer::sendJson(connection, 400,
            R"({"status":"error","message":"filename rejected by validator"})");
    }

    return HttpServer::sendJson(connection, 400,
        R"({"status":"error","message":"Upload failed"})");
}
