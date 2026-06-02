#include "serial_port.h"
#include "logger.h"
#include <cstring>
#include <unistd.h>

SerialPort::~SerialPort() {
    close();
}

bool SerialPort::open(const std::string& device, int baud_rate) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (port_) {
        LOG_WARN("Serial port already open, closing first");
        close();
    }

    device_ = device;

    sp_return ret = sp_get_port_by_name(device.c_str(), &port_);
    if (ret != SP_OK) {
        LOG_ERROR("Failed to get port '%s': %d", device.c_str(), ret);
        port_ = nullptr;
        return false;
    }

    ret = sp_open(port_, SP_MODE_READ_WRITE);
    if (ret != SP_OK) {
        LOG_ERROR("Failed to open port '%s': %d", device.c_str(), ret);
        sp_free_port(port_);
        port_ = nullptr;
        return false;
    }

    // Configure port: 8N1, no flow control
    sp_set_baudrate(port_, baud_rate);
    sp_set_bits(port_, 8);
    sp_set_parity(port_, SP_PARITY_NONE);
    sp_set_stopbits(port_, 1);
    sp_set_flowcontrol(port_, SP_FLOWCONTROL_NONE);

    LOG_INFO("Serial port '%s' opened at %d baud", device.c_str(), baud_rate);
    return true;
}

void SerialPort::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port_) {
        sp_close(port_);
        sp_free_port(port_);
        port_ = nullptr;
        LOG_INFO("Serial port '%s' closed", device_.c_str());
    }
}

bool SerialPort::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_ != nullptr;
}

bool SerialPort::writeLine(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!port_) return false;

    std::string data = cmd + "\r\n";
    ssize_t written = sp_blocking_write(port_, data.c_str(), data.size(), 1000);
    if (written < 0) {
        LOG_ERROR("Serial write error: %d", (int)written);
        return false;
    }

    LOG_DEBUG("Serial TX: %s", cmd.c_str());
    return true;
}

bool SerialPort::readLine(std::string& response, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!port_) return false;

    static const size_t MAX_LINE_LEN = 8192;

    response.clear();
    read_buf_.clear();

    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeout_ms) {
            LOG_WARN("Serial read timeout");
            return false;
        }

        char c;
        enum sp_return ret = sp_blocking_read_next(port_, &c, 1, 100);
        if (ret > 0) {
            if (c == '\n') {
                // Trim trailing \r if present
                if (!response.empty() && response.back() == '\r') {
                    response.pop_back();
                }

                // Reject lines exceeding max length
                if (response.size() > MAX_LINE_LEN) {
                    LOG_WARN("Serial RX: line too long (%zu bytes), discarding", response.size());
                    response.clear();
                    return false;
                }

                LOG_DEBUG("Serial RX: %s", response.c_str());
                return true;
            }
            response += c;
        } else if (ret < 0) {
            LOG_ERROR("Serial read error: %d", ret);
            return false;
        }
        // ret == 0 means timeout, continue looping
    }
}

bool SerialPort::sendCommand(const std::string& cmd, std::string& response, int timeout_ms) {
    if (!writeLine(cmd)) return false;
    return readLine(response, timeout_ms);
}
