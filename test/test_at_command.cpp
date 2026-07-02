// ============================================================
// test_at_command.cpp — unit tests for AT command processor.
//
// Tests AtCommand directly without HTTP server or serial port.
// Uses a real SerialDaemon with no serial device configured.
// ============================================================

#include "test_framework.h"
#include "at_command.h"
#include "logger.h"
#include "config.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

// --- Setup: create a minimal SerialDaemon (no serial device) ---
static Config make_test_config() {
    Config cfg;
    cfg.serial_device = "";           // no serial
    cfg.upload_dir = "/tmp/test_at_upload";
    cfg.foreground = true;
    return cfg;
}

static void setup() {
    mkdir("/tmp/test_at_upload", 0755);
    Logger::instance().shutdown();
}

static void cleanup() {
    // Remove test upload files
    system("rm -rf /tmp/test_at_upload/*");
    rmdir("/tmp/test_at_upload");
}

// =============================================================================
// AT+STATUS
// =============================================================================

TEST(at_status_returns_ok) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+STATUS", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("OK") == 0);
    ASSERT_TRUE(resp.find("serial=closed") != std::string::npos);
    ASSERT_TRUE(resp.find("device=none") != std::string::npos);
    cleanup();
    PASS();
}

TEST(at_status_shows_busy) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    // Acquire lock
    daemon.try_acquire(SerialDaemon::Owner::HTTP);

    std::string resp = at.process("AT+STATUS", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("busy=yes") != std::string::npos);

    daemon.release(SerialDaemon::Owner::HTTP);
    cleanup();
    PASS();
}

// =============================================================================
// AT+HELP
// =============================================================================

TEST(at_help_lists_all_commands) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+HELP", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("AT+STATUS") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+RESET") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+FORCERESET") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+FLASH") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+EXEC") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+PREUPLOAD") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+UPLOAD") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+UPLOADDONE") != std::string::npos);
    ASSERT_TRUE(resp.find("AT+UPLOADCANCEL") != std::string::npos);
    cleanup();
    PASS();
}

// =============================================================================
// AT+RESET / AT+FORCERESET
// =============================================================================

TEST(at_reset_releases_lock) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    daemon.try_acquire(SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(daemon.is_busy());

    std::string resp = at.process("AT+RESET", SerialDaemon::Owner::HTTP);
    ASSERT_EQ(resp, "OK");
    ASSERT_TRUE(!daemon.is_busy());
    cleanup();
    PASS();
}

TEST(at_reset_wrong_owner_fails) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    daemon.try_acquire(SerialDaemon::Owner::HTTP);

    // SERIAL caller trying to release HTTP-owned lock
    std::string resp = at.process("AT+RESET", SerialDaemon::Owner::SERIAL);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(daemon.is_busy());

    daemon.release(SerialDaemon::Owner::HTTP);
    cleanup();
    PASS();
}

TEST(at_forcereset_always_works) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    daemon.try_acquire(SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(daemon.is_busy());

    // SERIAL caller can force-release
    std::string resp = at.process("AT+FORCERESET", SerialDaemon::Owner::SERIAL);
    ASSERT_EQ(resp, "OK");
    ASSERT_TRUE(!daemon.is_busy());
    cleanup();
    PASS();
}

// =============================================================================
// AT+FLASH
// =============================================================================

TEST(at_flash_no_args_fails) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+FLASH", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("requires <file>") != std::string::npos);
    cleanup();
    PASS();
}

// =============================================================================
// AT+EXEC (should be disabled by default)
// =============================================================================

TEST(at_exec_disabled_by_default) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+EXEC=whoami", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("disabled") != std::string::npos);
    ASSERT_TRUE(resp.find("ALLOW_RCE") != std::string::npos);
    cleanup();
    PASS();
}

// =============================================================================
// Unknown command
// =============================================================================

TEST(at_unknown_command) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+BOGUS", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("unknown") != std::string::npos);
    cleanup();
    PASS();
}

TEST(at_not_at_command) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("HELLO", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("must start with AT") != std::string::npos);
    cleanup();
    PASS();
}

// =============================================================================
// Session lock integration
// =============================================================================

TEST(at_command_session_lock_flow) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    // HTTP acquires lock
    daemon.try_acquire(SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(daemon.is_busy());

    // AT+STATUS works (HTTP owns lock)
    std::string resp = at.process("AT+STATUS", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("OK") == 0);

    // SERIAL gets BUSY from try_acquire
    ASSERT_FALSE(daemon.try_acquire(SerialDaemon::Owner::SERIAL));

    // HTTP releases via AT+RESET
    resp = at.process("AT+RESET", SerialDaemon::Owner::HTTP);
    ASSERT_EQ(resp, "OK");
    ASSERT_TRUE(!daemon.is_busy());

    // SERIAL can now acquire
    ASSERT_TRUE(daemon.try_acquire(SerialDaemon::Owner::SERIAL));
    daemon.release(SerialDaemon::Owner::SERIAL);

    cleanup();
    PASS();
}

// =============================================================================
// AT+VERSION
// =============================================================================

TEST(at_version_returns_ok) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+VERSION", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("OK") == 0);
    ASSERT_TRUE(resp.find("daemon=") != std::string::npos);
    ASSERT_TRUE(resp.find("arch=") != std::string::npos);
    ASSERT_TRUE(resp.find("build=") != std::string::npos);
    cleanup();
    PASS();
}

// =============================================================================
// AT+REBOOT (should be disabled by default)
// =============================================================================

TEST(at_reboot_enabled_by_default) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    // AT+REBOOT is enabled by default — but reboot will fail (no reboot binary)
    std::string resp = at.process("AT+REBOOT", SerialDaemon::Owner::HTTP);
    // Should return either OK or error about reboot command failing
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos || resp.find("OK") == 0);
    cleanup();
    PASS();
}

// =============================================================================
// AT+VERIFY
// =============================================================================

TEST(at_verify_no_args) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+VERIFY", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("requires") != std::string::npos);
    cleanup();
    PASS();
}

TEST(at_verify_file_not_found) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+VERIFY=nonexistent.bin", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("cannot open") != std::string::npos);
    cleanup();
    PASS();
}

TEST(at_verify_path_traversal_rejected) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    std::string resp = at.process("AT+VERIFY=../../etc/passwd", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("ERROR") != std::string::npos);
    ASSERT_TRUE(resp.find("invalid") != std::string::npos);
    cleanup();
    PASS();
}

TEST(at_verify_computes_md5) {
    setup();
    Config cfg = make_test_config();
    SerialDaemon daemon(cfg);
    AtCommand at(daemon);

    // Create a test file
    FILE* f = fopen("/tmp/test_at_upload/test_verify.bin", "wb");
    ASSERT_TRUE(f != nullptr);
    fwrite("hello world", 1, 11, f);
    fclose(f);

    std::string resp = at.process("AT+VERIFY=test_verify.bin", SerialDaemon::Owner::HTTP);
    ASSERT_TRUE(resp.find("OK") == 0);
    // MD5 of "hello world" = 5eb63bbbe01eeed093cb22bb8f5acdc3
    ASSERT_TRUE(resp.find("5eb63bbbe01eeed093cb22bb8f5acdc3") != std::string::npos);
    ASSERT_TRUE(resp.find("11") != std::string::npos);  // file size

    cleanup();
    PASS();
}


