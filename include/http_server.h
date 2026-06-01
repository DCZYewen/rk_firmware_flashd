#pragma once

#include "config.h"
#include "serial_daemon.h"
#include <microhttpd.h>
#include <string>
#include <mutex>
#include <chrono>

// Forward declaration
class HttpServer;

// Per-connection state
struct ConnectionState {
    enum class RequestType {
        NONE,
        UPLOAD,
        COMMAND,
        STATUS,
        RESET
    };

    RequestType type;
    MHD_PostProcessor* pp;

    // Upload state
    std::string upload_filename;
    std::string upload_tmp_path;
    FILE* upload_file;
    uint64_t upload_size;

    // Command state
    std::string cmd_body;

    // Config reference for upload directory
    std::string upload_dir;

    ConnectionState()
        : type(RequestType::NONE), pp(nullptr), upload_file(nullptr), upload_size(0) {}

    ~ConnectionState();
};

class HttpServer {
public:
    HttpServer(const Config& cfg, SerialDaemon& daemon);
    ~HttpServer();

    bool start();
    void stop();
    bool is_running() const;

    // Accessible by handlers
    const Config& config() const { return cfg_; }
    SerialDaemon& daemon() { return serial_daemon_; }
    std::chrono::steady_clock::time_point start_time() const { return start_time_; }

    // Send a JSON response
    static MHD_Result sendJson(MHD_Connection* connection,
                               unsigned int status_code,
                               const std::string& json);

    // Helper to create a simple text response
    static MHD_Result sendText(MHD_Connection* connection,
                               unsigned int status_code,
                               const std::string& text);

private:
    static MHD_Result accessHandler(void* cls,
                                    MHD_Connection* connection,
                                    const char* url,
                                    const char* method,
                                    const char* version,
                                    const char* upload_data,
                                    size_t* upload_data_size,
                                    void** req_cls);

    const Config& cfg_;
    SerialDaemon& serial_daemon_;
    struct MHD_Daemon* mhd_daemon_;
    std::chrono::steady_clock::time_point start_time_;
};

// Handler declarations
MHD_Result handleUpload(void* cls, MHD_Connection* connection,
                        const char* method, const char* url,
                        const char* upload_data, size_t* upload_data_size,
                        ConnectionState* state, bool is_first_call);

MHD_Result handleCommand(void* cls, MHD_Connection* connection,
                         const char* method, const char* url,
                         const char* upload_data, size_t* upload_data_size,
                         ConnectionState* state, bool is_first_call);

MHD_Result handleStatus(void* cls, MHD_Connection* connection,
                        const char* method);

MHD_Result handleReset(void* cls, MHD_Connection* connection,
                       const char* method);
