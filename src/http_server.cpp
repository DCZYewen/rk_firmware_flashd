#include "http_server.h"
#include "logger.h"
#include <cstring>
#include <chrono>
#include <cstdio>
#include <unistd.h>

ConnectionState::~ConnectionState() {
    if (pp) {
        MHD_destroy_post_processor(pp);
    }
    if (upload_file) {
        fclose(upload_file);
        upload_file = nullptr;
        // Remove incomplete upload on error
        if (upload_size == 0 && !upload_tmp_path.empty()) {
            unlink(upload_tmp_path.c_str());
        }
    }
}

HttpServer::HttpServer(const Config& cfg, SerialDaemon& daemon)
    : cfg_(cfg), serial_daemon_(daemon),
      start_time_(std::chrono::steady_clock::now()) {
    // Default filename validator: reject path traversal and dangerous names
    validate_filename_ = [](const std::string& name) -> bool {
        if (name.empty() || name.size() > 255) return false;
        if (name == "." || name == "..") return false;
        if (name.find('/') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false;
        if (name.find("..") != std::string::npos) return false;
        for (char c : name) {
            if (!isalnum(c) && c != '-' && c != '_' && c != '.') return false;
        }
        return true;
    };
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    mhd_daemon_ = MHD_start_daemon(
        MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
        cfg_.port,
        nullptr, nullptr,
        accessHandler, this,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)120,
        MHD_OPTION_END);

    if (!mhd_daemon_) {
        LOG_ERROR("Failed to start HTTP server on port %d", cfg_.port);
        return false;
    }

    LOG_INFO("HTTP server started on port %d", cfg_.port);
    return true;
}

void HttpServer::stop() {
    if (mhd_daemon_) {
        MHD_stop_daemon(mhd_daemon_);
        mhd_daemon_ = nullptr;
        LOG_INFO("HTTP server stopped");
    }
}

bool HttpServer::is_running() const {
    return mhd_daemon_ != nullptr;
}

MHD_Result HttpServer::sendJson(MHD_Connection* connection,
                                 unsigned int status_code,
                                 const std::string& json) {
    struct MHD_Response* response = MHD_create_response_from_buffer(
        json.size(), (void*)json.c_str(), MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

MHD_Result HttpServer::sendText(MHD_Connection* connection,
                                 unsigned int status_code,
                                 const std::string& text) {
    struct MHD_Response* response = MHD_create_response_from_buffer(
        text.size(), (void*)text.c_str(), MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "text/plain");
    MHD_Result ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

MHD_Result HttpServer::accessHandler(void* cls,
                                      MHD_Connection* connection,
                                      const char* url,
                                      const char* method,
                                      const char* version,
                                      const char* upload_data,
                                      size_t* upload_data_size,
                                      void** req_cls) {
    HttpServer* self = static_cast<HttpServer*>(cls);

    // First call — initialize connection state
    if (*req_cls == nullptr) {
        ConnectionState* state = new ConnectionState();
        state->upload_dir = self->config().upload_dir;
        state->server = self;

        // Determine request type from URL
        if (strcmp(url, "/api/upload") == 0 && strcmp(method, "POST") == 0) {
            state->type = ConnectionState::RequestType::UPLOAD;
            state->pp = MHD_create_post_processor(connection, 65536,
                [](void* cls, enum MHD_ValueKind kind, const char* key,
                   const char* filename, const char* content_type,
                   const char* transfer_encoding, const char* data,
                   uint64_t off, size_t size) -> enum MHD_Result {
                    (void)kind;
                    (void)content_type;
                    (void)transfer_encoding;
                    (void)off;

                    ConnectionState* state = static_cast<ConnectionState*>(cls);

                    if (filename && key && strcmp(key, "firmware") == 0) {
                        // Validate filename before accepting
                        if (state->server && !state->server->validate_filename()(filename)) {
                            LOG_WARN("POST processor: rejected filename '%s'", filename);
                            return MHD_YES;  // skip this part, don't open file
                        }

                        // First call with filename — open the output file
                        if (state->upload_tmp_path.empty()) {
                            state->upload_filename = filename;

                            // Generate unique temp filename
                            auto now = std::chrono::system_clock::now();
                            auto ts = std::chrono::system_clock::to_time_t(now);
                            std::string tmp_name = std::to_string(ts) + "_" + filename;
                            state->upload_tmp_path = state->upload_dir + "/" + tmp_name;

                            state->upload_file = fopen(state->upload_tmp_path.c_str(), "wb");
                            if (!state->upload_file) {
                                LOG_ERROR("Failed to create upload file: %s", state->upload_tmp_path.c_str());
                                return MHD_NO;
                            }
                        }
                    }

                    if (state->upload_file && data && size > 0) {
                        fwrite(data, 1, size, state->upload_file);
                        state->upload_size += size;
                    }

                    return MHD_YES;
                }, state);
        } else if (strcmp(url, "/api/command") == 0 && strcmp(method, "POST") == 0) {
            state->type = ConnectionState::RequestType::COMMAND;
        } else if (strcmp(url, "/api/status") == 0 && strcmp(method, "GET") == 0) {
            state->type = ConnectionState::RequestType::STATUS;
        } else if (strcmp(url, "/api/reset") == 0 && strcmp(method, "POST") == 0) {
            state->type = ConnectionState::RequestType::RESET;
        }

        *req_cls = state;
        return MHD_YES;
    }

    ConnectionState* state = static_cast<ConnectionState*>(*req_cls);
    bool is_first_call = (*upload_data_size == 0);

    switch (state->type) {
        case ConnectionState::RequestType::UPLOAD:
            return handleUpload(cls, connection, method, url,
                              upload_data, upload_data_size, state, is_first_call);
        case ConnectionState::RequestType::COMMAND:
            return handleCommand(cls, connection, method, url,
                               upload_data, upload_data_size, state, is_first_call);
        case ConnectionState::RequestType::STATUS:
            return handleStatus(cls, connection, method);
        case ConnectionState::RequestType::RESET:
            return handleReset(cls, connection, method);
        default:
            return HttpServer::sendJson(connection, 404,
                R"({"status":"error","message":"Not found"})");
    }
}
