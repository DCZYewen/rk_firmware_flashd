#include "serial_daemon.h"
#include "logger.h"

#include <chrono>
#include <mutex>

// =============================================================================
// Construction / destruction
// =============================================================================

SerialDaemon::SerialDaemon(const Config& cfg)
    : cfg_(cfg) {}

SerialDaemon::~SerialDaemon() {
    stop();
}

// =============================================================================
// Lifecycle
// =============================================================================

void SerialDaemon::start() {
    if (!cfg_.use_serial()) {
        LOG_INFO("SerialDaemon: no serial device configured");
        return;
    }

    std::lock_guard<std::mutex> io(io_mutex_);
    if (serial_.open(cfg_.serial_device, cfg_.baud_rate)) {
        LOG_INFO("SerialDaemon: opened '%s' at %d baud",
                 cfg_.serial_device.c_str(), cfg_.baud_rate);
    } else {
        LOG_WARN("SerialDaemon: failed to open '%s'",
                 cfg_.serial_device.c_str());
    }
}

void SerialDaemon::stop() {
    stop_reader();
    std::lock_guard<std::mutex> io(io_mutex_);
    if (serial_.is_open()) {
        serial_.close();
        LOG_INFO("SerialDaemon: port closed");
    }
    // Release global lock on shutdown (force-release, ignore owner).
    Owner prev = owner_.exchange(Owner::NONE, std::memory_order_acq_rel);
    if (prev != Owner::NONE) {
        LOG_INFO("SerialDaemon: lock force-released on shutdown");
    }
}

// =============================================================================
// Global lock (preemptive, non-blocking) — atomic CAS
// =============================================================================

bool SerialDaemon::try_acquire(Owner who) {
    Owner expected = Owner::NONE;
    if (owner_.compare_exchange_strong(expected, who,
                                       std::memory_order_acq_rel)) {
        LOG_INFO("SerialDaemon: lock acquired by %s",
                 who == Owner::HTTP ? "HTTP" : "SERIAL");
        return true;
    }
    return false;
}

void SerialDaemon::release(Owner who) {
    Owner expected = who;
    if (owner_.compare_exchange_strong(expected, Owner::NONE,
                                       std::memory_order_acq_rel)) {
        LOG_INFO("SerialDaemon: lock released by %s",
                 who == Owner::HTTP ? "HTTP" : "SERIAL");
    } else {
        LOG_WARN("SerialDaemon: release denied — caller is not owner");
    }
}

void SerialDaemon::force_release() {
    Owner prev = owner_.exchange(Owner::NONE, std::memory_order_acq_rel);
    if (prev != Owner::NONE) {
        std::string who = (prev == Owner::HTTP) ? "HTTP" : "SERIAL";
        LOG_INFO("SerialDaemon: lock force-released (was %s)", who.c_str());
    }
}

SerialDaemon::Owner SerialDaemon::owner() const {
    return owner_.load(std::memory_order_acquire);
}

bool SerialDaemon::is_busy() const {
    return owner_.load(std::memory_order_acquire) != Owner::NONE;
}

// =============================================================================
// Queries
// =============================================================================

bool SerialDaemon::is_open() const {
    std::lock_guard<std::mutex> io(io_mutex_);
    return serial_.is_open();
}

// =============================================================================
// Low-level serial I/O (under io_mutex_)
// =============================================================================

bool SerialDaemon::readLine(std::string& line, int timeout_ms) {
    std::lock_guard<std::mutex> io(io_mutex_);
    if (!serial_.is_open()) return false;
    return serial_.readLine(line, timeout_ms);
}

bool SerialDaemon::writeLine(const std::string& line) {
    std::lock_guard<std::mutex> io(io_mutex_);
    if (!serial_.is_open()) return false;
    return serial_.writeLine(line);
}

// =============================================================================
// Serial reader thread
// =============================================================================
//
// Continuously reads AT command lines from the serial device and feeds them
// through the shared CommandHandler.  If the global lock is held by the HTTP
// daemon, the serial reader returns "BUSY" to the device and keeps looping.

void SerialDaemon::start_reader(CommandHandler handler) {
    if (!cfg_.use_serial()) {
        LOG_INFO("SerialDaemon: no serial device, reader not started");
        return;
    }
    if (reader_running_.load()) {
        LOG_WARN("SerialDaemon: reader already running");
        return;
    }

    reader_running_.store(true);
    reader_thread_ = std::thread([this, handler]() {
        LOG_INFO("SerialDaemon: reader loop started");

        while (reader_running_.load()) {
            // Read a line from serial (no lock needed yet).
            std::string line;
            if (!readLine(line, 500)) {
                if (!reader_running_.load()) break;
                // Timeout or error — try to reconnect.
                {
                    std::lock_guard<std::mutex> io(io_mutex_);
                    if (!serial_.is_open()) {
                        if (!serial_.open(cfg_.serial_device, cfg_.baud_rate)) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }
                        LOG_INFO("SerialDaemon: reader reconnected");
                    }
                }
                continue;
            }

            LOG_DEBUG("SerialDaemon: reader got '%s'", line.c_str());

            // Acquire or reuse the lock for SERIAL.
            if (!try_acquire(Owner::SERIAL)) {
                if (owner() != Owner::SERIAL) {
                    // HTTP holds the lock — tell the device.
                    writeLine("BUSY");
                    continue;
                }
                // SERIAL already owns the lock — proceed.
            }

            // Process the command under the lock.
            std::string response = handler(line);

            // Write response back on serial.
            if (!response.empty()) {
                writeLine(response);
            }

            // Note: lock is NOT released here — only AT+RESET releases it.
            // The handler should have called release() if the command was
            // AT+RESET.  For other commands, the lock stays held.
        }

        LOG_INFO("SerialDaemon: reader loop exited");
    });
    LOG_INFO("SerialDaemon: reader thread started");
}

void SerialDaemon::stop_reader() {
    if (!reader_running_.load()) return;

    reader_running_.store(false);

    // Wake up the reader if it's blocked on a read.
    {
        std::lock_guard<std::mutex> io(io_mutex_);
        if (serial_.is_open()) {
            serial_.close();
        }
    }

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    LOG_INFO("SerialDaemon: reader thread stopped");
}
