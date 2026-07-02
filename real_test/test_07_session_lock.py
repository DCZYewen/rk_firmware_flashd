#!/usr/bin/env python3
"""Section 7: Session Lock Test — each step is a separate test."""

from utils import *

def test_acquire():
    code, j = cmd("AT+STATUS")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")

def test_status_shows_busy():
    code, j = api("GET", "/api/status")
    assert_eq(code, 200)
    assert_eq(j["daemon_busy"], True)
    assert_eq(j["lock_owner"], "HTTP")

def test_release():
    code, j = cmd("AT+RESET")
    assert_eq(code, 200)
    assert_eq(j["response"], "OK")

def test_status_shows_free():
    code, j = api("GET", "/api/status")
    assert_eq(code, 200)
    assert_eq(j["daemon_busy"], False)

def test_reset_no_owner_fails():
    code, j = cmd("AT+RESET")
    assert_in(j["response"], "only the lock owner can reset")

if __name__ == "__main__":
    print("\n[7. Session Lock Test]")
    test("AT+STATUS acquires lock", test_acquire)
    test("Status shows busy + owner=HTTP", test_status_shows_busy)
    test("AT+RESET releases lock", test_release)
    test("Status shows free", test_status_shows_free)

    # Test: AT+RESET with no owner should fail
    test("AT+RESET without owner errors", test_reset_no_owner_fails)

    p, f = results()
    print(f"  {p}/{p+f} passed")
