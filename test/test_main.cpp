// ============================================================
// test_main.cpp — provides main() and the test counters.
//
// Link this with your test_*.cpp files. main() runs AFTER all
// tests have already executed (via static constructors), so it
// just prints the summary and returns an exit code.
//
// Exit code:
//   0 = all tests passed
//   1 = one or more tests failed
//
// This lets CTest (or any test runner) detect pass/fail
// simply by checking the exit code.
// ============================================================

#include "test_framework.h"

// Global counters — incremented by ASSERT_*/PASS macros.
int g_passed = 0;
int g_failed = 0;

int main() {
    fprintf(stderr, "\n=== Test Results ===\n\n");

    // At this point, all TEST() functions have already run
    // (their static constructors executed before main).
    // g_passed and g_failed already have their final values.

    fprintf(stderr, "--- %d passed, %d failed ---\n\n",
            g_passed, g_failed);

    // Exit code 0 = success, 1 = failure.
    // CTest checks this to report Pass/Fail.
    return g_failed > 0 ? 1 : 0;
}
