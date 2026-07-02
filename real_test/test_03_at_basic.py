#!/usr/bin/env python3
"""Section 3: AT Commands — Basic"""

from utils import *

def test_status():
    code, j = cmd("AT+STATUS")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")
    assert_in(j["response"], "OK")

def test_version():
    code, j = cmd("AT+VERSION")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")
    assert_in(j["response"], "daemon=")

def test_help():
    code, j = cmd("AT+HELP")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")
    assert_in(j["response"], "AT+FLASH")

if __name__ == "__main__":
    print("\n[3. AT Commands — Basic]")
    test("AT+STATUS", test_status)
    test("AT+VERSION", test_version)
    test("AT+HELP", test_help)
    p, f = results(); print(f"  {p}/{p+f} passed")
