#pragma once

#include <string>
#include <mutex>
#include <cstdint>
#include <libserialport.h>

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Open the serial port with given device path and baud rate.
    bool open(const std::string& device, int baud_rate);

    // Close the serial port.
    void close();

    // Check if port is open.
    bool is_open() const;

    // Send a command line (appends \r\n).
    bool writeLine(const std::string& cmd);

    // Read a response line until \n, with timeout in milliseconds.
    // Returns false on timeout or error.
    bool readLine(std::string& response, int timeout_ms = 5000);

    // Send a command and read the full response.
    bool sendCommand(const std::string& cmd, std::string& response, int timeout_ms = 5000);

    // Get the device path.
    const std::string& device() const { return device_; }

private:
    sp_port* port_ = nullptr;
    int raw_fd_ = -1;           // fallback fd when libserialport fails (PTY etc.)
    std::string device_;
    mutable std::mutex mutex_;
    std::string read_buf_;
};
