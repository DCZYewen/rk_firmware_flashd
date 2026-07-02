#!/usr/bin/env python3
"""Section 4: Async Flash — background thread version."""

import time
from utils import *

def drain_flash(timeout=10):
    """Poll AT+TRYFLASHDONE until done or timeout. Returns last response."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        _, j = cmd("AT+TRYFLASHDONE")
        r = j.get("response", "")
        if not r.startswith("RUNNING"):
            return r
        time.sleep(0.2)
    return "TIMEOUT"

def test_submit():
    code, j = cmd("AT+FLASH=test_fw.bin")
    assert_eq(code, 200)
    assert_in(j["response"], "submitted")

def test_poll_shows_running():
    code, j = cmd("AT+TRYFLASHDONE")
    assert_eq(code, 200)
    assert_in(j["response"], "RUNNING")

def test_wait_for_result():
    r = drain_flash()
    assert_in(r, "OK")  # script exits 0
    assert_in(r, "FULL flash: test_fw.bin")

def test_second_poll_no_pending():
    r = drain_flash()
    r2 = drain_flash()
    assert_in(r2, "no pending")

def test_reject_concurrent():
    cmd("AT+FLASH=test_fw.bin")
    code, j = cmd("AT+FLASH=other.bin")
    assert_eq(code, 200)
    assert_in(j["response"], "already in progress")
    drain_flash()  # clean up

def test_no_args():
    code, j = cmd("AT+FLASH")
    assert_in(j["response"], "requires <file>")

def test_bad_mode():
    code, j = cmd("AT+FLASH=test.bin,BOGUS")
    assert_in(j["response"], "unknown flash mode")

if __name__ == "__main__":
    print("\n[4. AT Commands — Async Flash]")

    test("AT+FLASH submit", test_submit)
    test("Immediate poll shows RUNNING", test_poll_shows_running)
    test("Wait for completion (poll loop)", test_wait_for_result)
    test("Second poll shows no pending", test_second_poll_no_pending)

    test("Reject concurrent", test_reject_concurrent)
    test("No args validation", test_no_args)
    test("Bad mode validation", test_bad_mode)

    p, f = results()
    print(f"  {p}/{p+f} passed")
