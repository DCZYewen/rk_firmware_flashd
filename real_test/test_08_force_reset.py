#!/usr/bin/env python3
"""Section 8: Force Reset — each step is a separate test."""

from utils import *

def test_acquire():
    cmd("AT+STATUS")

def test_force_reset_api():
    code, j = api("POST", "/api/reset")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")

def test_verify_released():
    code, j = api("GET", "/api/status")
    assert_eq(code, 200)
    assert_eq(j["daemon_busy"], False)

def test_reset_no_lock():
    code, j = api("POST", "/api/reset")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")

if __name__ == "__main__":
    print("\n[8. Force Reset]")
    test("AT+STATUS acquire lock", test_acquire)
    test("POST /api/reset force release", test_force_reset_api)
    test("Verify lock released", test_verify_released)
    test("POST /api/reset with no lock", test_reset_no_lock)
    p, f = results()
    print(f"  {p}/{p+f} passed")
