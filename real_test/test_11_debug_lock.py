#!/usr/bin/env python3
"""Section 11: Cross-Owner Lock Test (Debug Only)."""

from utils import *

def test_acquire_serial():
    api("POST", "/api/reset")  # ensure clean state
    code, j = api("POST", "/api/debug/lock-serial")
    assert_eq(code, 200)
    assert_eq(j["message"], "Lock acquired for SERIAL")

def test_reset_rejected():
    code, j = cmd("AT+RESET")
    assert_in(j["response"], "only the lock owner can reset")

def test_status_shows_serial():
    code, j = api("GET", "/api/status")
    assert_eq(j["lock_owner"], "SERIAL")

def test_forcereset_works():
    code, j = cmd("AT+FORCERESET")
    assert_eq(j["response"], "OK")

def test_status_shows_free():
    code, j = api("GET", "/api/status")
    assert_eq(j["lock_owner"], None)

def test_acquire_again():
    code, j = api("POST", "/api/debug/lock-serial")
    assert_eq(code, 200)
    assert_eq(j["message"], "Lock acquired for SERIAL")
    api("POST", "/api/reset")  # clean up

if __name__ == "__main__":
    print("\n[11. Cross-Owner Lock Test (Debug)]")
    test("Force reset + acquire SERIAL", test_acquire_serial)
    test("AT+RESET rejected (wrong owner)", test_reset_rejected)
    test("Status shows lock_owner=SERIAL", test_status_shows_serial)
    test("AT+FORCERESET works regardless", test_forcereset_works)
    test("Status shows lock_owner=null", test_status_shows_free)
    test("Re-acquire + cleanup", test_acquire_again)
    p, f = results()
    print(f"  {p}/{p+f} passed")
