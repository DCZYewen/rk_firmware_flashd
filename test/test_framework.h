#pragma once

// ============================================================
// Minimal C++14 test framework — no external dependencies.
//
// Usage:
//   1. #include "test_framework.h" in your test file.
//   2. Write tests with TEST(name) { ... PASS(); }
//   3. Link against test_main.cpp which provides main() and
//      prints the final summary.
//
// How it works:
//   TEST(name) expands to a static struct whose constructor
//   runs the test BEFORE main(). So all tests execute on their
//   own — main() just prints the results.
// ============================================================

#include <cstdio>
#include <cstring>
#include <string>

// --- Counters (defined in test_main.cpp) ---
extern int g_passed;
extern int g_failed;

// --- Test registration macro ---
//
// For TEST(my_test) this creates:
//   1. static void test_my_test()    — the actual test body
//   2. struct TestReg_my_test        — static object whose
//      constructor calls test_my_test() before main()
//
#define TEST(name) \
    static void test_##name(); \
    struct TestReg_##name { \
        TestReg_##name() { test_##name(); } \
    } reg_##name; \
    static void test_##name()

// --- Assertion macros ---
//
// On failure: prints file:line + expression, increments g_failed,
//             and RETURNS from the test (skipping PASS).
// On success: execution continues to the next line.

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", \
                __FILE__, __LINE__, #expr); \
        g_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s\n", \
                __FILE__, __LINE__, #a, #b); \
        g_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, (a), (b)); \
        g_failed++; \
        return; \
    } \
} while(0)

// --- PASS macro ---
//
// Call at the end of a test to count it as passed.
// If any ASSERT_* failed before this, we already returned —
// so this line is never reached on failure.

#define PASS() do { \
    fprintf(stderr, "  PASS\n"); \
    g_passed++; \
} while(0)

// --- Helper utilities ---

// Read an entire file into a string (returns "" on error).
static inline std::string read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string content(len, '\0');
    fread(&content[0], 1, len, f);
    fclose(f);
    return content;
}

// Count how many times `needle` appears in `haystack`.
static inline int count_occurrences(const std::string& haystack,
                                    const std::string& needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        count++;
        pos += needle.size();
    }
    return count;
}
