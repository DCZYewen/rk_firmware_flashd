// ============================================================
// test_logger.cpp — unit tests for the Logger class.
//
// Each TEST(name) block is self-contained. The framework
// (test_framework.h) handles registration and counting.
// The entry point main() lives in test_main.cpp.
// ============================================================

#include "test_framework.h"
#include "logger.h"
#include <cstdlib>
#include <unistd.h>

static const char* TEST_LOG = "/tmp/test_rk_flashd.log";

// --- Helper: clean up before and after each test ---
static void setup() {
    unlink(TEST_LOG);
    Logger::instance().shutdown();
}

// --- Tests ---

TEST(singleton_returns_same_instance) {
    Logger& a = Logger::instance();
    Logger& b = Logger::instance();
    ASSERT_TRUE(&a == &b);
    PASS();
}

TEST(init_with_file_creates_file) {
    setup();
    Logger::instance().init(TEST_LOG);

    Logger::instance().log(LogLevel::INFO, "test.cpp", 42, "hello from test");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("hello from test") != std::string::npos);
    ASSERT_TRUE(content.find("[INFO ]") != std::string::npos);
    ASSERT_TRUE(content.find("test.cpp:42") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(log_level_in_timestamp_format) {
    setup();
    Logger::instance().init(TEST_LOG);

    Logger::instance().log(LogLevel::DEBUG, "t.cpp", 1, "msg-debug");
    Logger::instance().log(LogLevel::INFO,  "t.cpp", 2, "msg-info");
    Logger::instance().log(LogLevel::WARN,  "t.cpp", 3, "msg-warn");
    Logger::instance().log(LogLevel::ERROR, "t.cpp", 4, "msg-error");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("[DEBUG]") != std::string::npos);
    ASSERT_TRUE(content.find("[INFO ]") != std::string::npos);
    ASSERT_TRUE(content.find("[WARN ]") != std::string::npos);
    ASSERT_TRUE(content.find("[ERROR]") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(set_level_filters_messages) {
    setup();
    Logger::instance().init(TEST_LOG);
    Logger::instance().set_level(LogLevel::WARN);

    Logger::instance().log(LogLevel::DEBUG, "t.cpp", 1, "should-not-appear-debug");
    Logger::instance().log(LogLevel::INFO,  "t.cpp", 2, "should-not-appear-info");
    Logger::instance().log(LogLevel::WARN,  "t.cpp", 3, "should-appear-warn");
    Logger::instance().log(LogLevel::ERROR, "t.cpp", 4, "should-appear-error");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("should-not-appear-debug") == std::string::npos);
    ASSERT_TRUE(content.find("should-not-appear-info") == std::string::npos);
    ASSERT_TRUE(content.find("should-appear-warn") != std::string::npos);
    ASSERT_TRUE(content.find("should-appear-error") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(set_level_debug_all_passes) {
    setup();
    Logger::instance().init(TEST_LOG);
    Logger::instance().set_level(LogLevel::DEBUG);

    Logger::instance().log(LogLevel::DEBUG, "t.cpp", 1, "debug-msg");
    Logger::instance().log(LogLevel::INFO,  "t.cpp", 2, "info-msg");
    Logger::instance().log(LogLevel::WARN,  "t.cpp", 3, "warn-msg");
    Logger::instance().log(LogLevel::ERROR, "t.cpp", 4, "error-msg");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_EQ(count_occurrences(content, "-msg"), 4);
    unlink(TEST_LOG);
    PASS();
}

TEST(set_level_error_only) {
    setup();
    Logger::instance().init(TEST_LOG);
    Logger::instance().set_level(LogLevel::ERROR);

    Logger::instance().log(LogLevel::DEBUG, "t.cpp", 1, "nope-debug");
    Logger::instance().log(LogLevel::INFO,  "t.cpp", 2, "nope-info");
    Logger::instance().log(LogLevel::WARN,  "t.cpp", 3, "nope-warn");
    Logger::instance().log(LogLevel::ERROR, "t.cpp", 4, "yes-error");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("nope-debug") == std::string::npos);
    ASSERT_TRUE(content.find("nope-info") == std::string::npos);
    ASSERT_TRUE(content.find("nope-warn") == std::string::npos);
    ASSERT_TRUE(content.find("yes-error") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(log_macros_work) {
    setup();
    Logger::instance().init(TEST_LOG);
    Logger::instance().set_level(LogLevel::DEBUG);

    LOG_DEBUG("macro debug %d", 1);
    LOG_INFO("macro info %s", "hi");
    LOG_WARN("macro warn %d", 3);
    LOG_ERROR("macro error %d", 4);
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("macro debug 1") != std::string::npos);
    ASSERT_TRUE(content.find("macro info hi") != std::string::npos);
    ASSERT_TRUE(content.find("macro warn 3") != std::string::npos);
    ASSERT_TRUE(content.find("macro error 4") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(timestamp_format_is_correct) {
    setup();
    Logger::instance().init(TEST_LOG);

    Logger::instance().log(LogLevel::INFO, "t.cpp", 1, "ts-test");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    // Expected: [YYYY-MM-DD HH:MM:SS.mmm] [INFO ] (t.cpp:1) ts-test
    ASSERT_TRUE(content.find("[20") != std::string::npos);
    ASSERT_TRUE(content.find("] [INFO ]") != std::string::npos);
    ASSERT_TRUE(content.find("(t.cpp:1)") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(file_path_shortening) {
    setup();
    Logger::instance().init(TEST_LOG);

    Logger::instance().log(LogLevel::INFO, "/home/user/project/src/main.cpp", 100, "path-test");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("main.cpp:100") != std::string::npos);
    ASSERT_TRUE(content.find("/home/user") == std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(shutdown_closes_file_gracefully) {
    setup();
    Logger::instance().init(TEST_LOG);

    Logger::instance().log(LogLevel::INFO, "t.cpp", 1, "before-shutdown");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("before-shutdown") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(reinit_after_shutdown_works) {
    unlink(TEST_LOG);

    Logger::instance().shutdown();
    Logger::instance().init(TEST_LOG);
    Logger::instance().log(LogLevel::INFO, "t.cpp", 1, "cycle-1");
    Logger::instance().shutdown();

    Logger::instance().init(TEST_LOG);
    Logger::instance().log(LogLevel::INFO, "t.cpp", 2, "cycle-2");
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    ASSERT_TRUE(content.find("cycle-1") != std::string::npos);
    ASSERT_TRUE(content.find("cycle-2") != std::string::npos);
    unlink(TEST_LOG);
    PASS();
}

TEST(multiple_messages_appended) {
    setup();
    Logger::instance().init(TEST_LOG);

    for (int i = 0; i < 10; i++) {
        Logger::instance().log(LogLevel::INFO, "t.cpp", i,
                              ("line-" + std::to_string(i)).c_str());
    }
    Logger::instance().shutdown();

    std::string content = read_file(TEST_LOG);
    for (int i = 0; i < 10; i++) {
        std::string needle = "line-" + std::to_string(i);
        ASSERT_TRUE(content.find(needle) != std::string::npos);
    }
    unlink(TEST_LOG);
    PASS();
}
