#pragma once

// =============================================================================
// SerialDaemon — Serial port manager with a preemptive global lock.
//
// MODEL
// =====
// Two daemon threads (HTTP and serial reader) share a single global lock.
// The first to acquire it becomes the OWNER.  Only the owner can release
// the lock via AT+RESET.  The other side gets "BUSY" — non-blocking.
//
//   HTTP thread:      try_acquire(HTTP) → process → hold → AT+RESET releases
//   Serial thread:    try_acquire(SERIAL) → process → hold → AT+RESET releases
//
// If HTTP acquires first, serial is busy until HTTP resets.
// If serial acquires first, HTTP is busy until serial resets.
// =============================================================================

#include "config.h"
#include "serial_port.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class SerialDaemon {
public:
    // Who owns the global lock?
    enum class Owner : uint8_t {
        NONE,
        HTTP,
        SERIAL,
    };

    using CommandHandler = std::function<std::string(const std::string&)>;

    explicit SerialDaemon(const Config& cfg);
    ~SerialDaemon();

    SerialDaemon(const SerialDaemon&) = delete;
    SerialDaemon& operator=(const SerialDaemon&) = delete;

    // ---- Lifecycle --------------------------------------------------------

    void start();
    void stop();

    // ---- Serial reader thread ---------------------------------------------

    void start_reader(CommandHandler handler);
    void stop_reader();

    // ---- Global lock (preemptive, non-blocking) ---------------------------

    // Try to acquire the global lock for the given owner.
    // Returns true if acquired, false if already held by someone else.
    bool try_acquire(Owner who);

    // Release the global lock.  Only succeeds if the caller is the owner.
    void release(Owner who);

    // Force-release the lock regardless of owner (administrative override).
    void force_release();

    // Query current owner.
    Owner owner() const;

    // Is the lock currently held?
    bool is_busy() const;

    // ---- Serial I/O (under io_mutex_) -------------------------------------

    bool readLine(std::string& line, int timeout_ms = 1000);
    bool writeLine(const std::string& line);

    // ---- Queries ----------------------------------------------------------

    bool is_open() const;
    bool serial_enabled() const { return cfg_.use_serial(); }
    const std::string& device() const { return cfg_.serial_device; }
    const Config& config() const { return cfg_; }

private:
    Config cfg_;
    SerialPort serial_;

    // Serial I/O mutex — protects readLine / writeLine / open / close
    mutable std::mutex io_mutex_;

    // Global lock — only one daemon processes at a time
    // Uses atomic CAS so force_release() is safe from any thread.
    std::atomic<Owner> owner_{Owner::NONE};

    // Reader thread
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{false};
};
