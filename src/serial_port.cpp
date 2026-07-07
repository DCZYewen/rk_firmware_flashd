#include "serial_port.h"
#include "logger.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

SerialPort::~SerialPort() {
    close();
}

// ---------------------------------------------------------------------------
// Convert baud rate to termios constant
// ---------------------------------------------------------------------------
static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

// ---------------------------------------------------------------------------
// Open via POSIX directly (fallback for PTY and unsupported devices)
// ---------------------------------------------------------------------------
static int open_posix(const std::string& device, int baud_rate) {
    int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        LOG_ERROR("POSIX open failed for '%s'", device.c_str());
        return -1;
    }

    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    cfsetospeed(&tio, baud_to_speed(baud_rate));
    cfsetispeed(&tio, baud_to_speed(baud_rate));
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;          // raw mode
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;      // 100ms timeout

    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        LOG_ERROR("tcsetattr failed for '%s'", device.c_str());
        ::close(fd);
        return -1;
    }

    LOG_INFO("Serial port '%s' opened via POSIX at %d baud", device.c_str(), baud_rate);
    return fd;
}

// =============================================================================
// SerialPort implementation
// =============================================================================

bool SerialPort::open(const std::string& device, int baud_rate) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (port_ || raw_fd_ >= 0) {
        LOG_WARN("Serial port already open, closing first");
        close();
    }

    device_ = device;

    // Try libserialport first
    sp_return ret = sp_get_port_by_name(device.c_str(), &port_);
    if (ret == SP_OK) {
        ret = sp_open(port_, SP_MODE_READ_WRITE);
        if (ret == SP_OK) {
            sp_set_baudrate(port_, baud_rate);
            sp_set_bits(port_, 8);
            sp_set_parity(port_, SP_PARITY_NONE);
            sp_set_stopbits(port_, 1);
            sp_set_flowcontrol(port_, SP_FLOWCONTROL_NONE);
            LOG_INFO("Serial port '%s' opened via libserialport at %d baud", device.c_str(), baud_rate);
            return true;
        }
        LOG_WARN("libserialport sp_open failed for '%s': %d, trying POSIX fallback", device.c_str(), ret);
        sp_free_port(port_);
        port_ = nullptr;
    } else {
        LOG_WARN("libserialport get_port_by_name failed for '%s': %d, trying POSIX fallback", device.c_str(), ret);
    }

    // Fallback: POSIX open + tcsetattr
    raw_fd_ = open_posix(device, baud_rate);
    if (raw_fd_ >= 0) {
        return true;
    }

    LOG_ERROR("Failed to open port '%s' (both libserialport and POSIX)", device.c_str());
    return false;
}

void SerialPort::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port_) {
        sp_close(port_);
        sp_free_port(port_);
        port_ = nullptr;
    }
    if (raw_fd_ >= 0) {
        ::close(raw_fd_);
        raw_fd_ = -1;
    }
    if (!device_.empty()) {
        LOG_INFO("Serial port '%s' closed", device_.c_str());
    }
}

bool SerialPort::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_ != nullptr || raw_fd_ >= 0;
}

bool SerialPort::writeLine(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string data = cmd + "\r\n";

    if (port_) {
        ssize_t written = sp_blocking_write(port_, data.c_str(), data.size(), 1000);
        if (written < 0) {
            LOG_ERROR("Serial write error: %d", (int)written);
            return false;
        }
    } else if (raw_fd_ >= 0) {
        ssize_t written = ::write(raw_fd_, data.c_str(), data.size());
        if (written < 0) {
            LOG_ERROR("Serial write error: %s", strerror(errno));
            return false;
        }
    } else {
        return false;
    }

    LOG_DEBUG("Serial TX: %s", cmd.c_str());
    return true;
}

bool SerialPort::readLine(std::string& response, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!port_ && raw_fd_ < 0) return false;

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

        char c = 0;
        int ret = 0;

        if (port_) {
            ret = sp_blocking_read_next(port_, &c, 1, 100);
        } else {
            ret = ::read(raw_fd_, &c, 1);
            if (ret > 0) ret = 1;  // normalize: 1 byte read
        }

        if (ret > 0) {
            if (c == '\n') {
                if (!response.empty() && response.back() == '\r')
                    response.pop_back();
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
        // ret == 0 means timeout (libserialport) or no data (POSIX), continue looping
    }
}

bool SerialPort::sendCommand(const std::string& cmd, std::string& response, int timeout_ms) {
    if (!writeLine(cmd)) return false;
    return readLine(response, timeout_ms);
}
