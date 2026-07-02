#!/usr/bin/env python3
"""Section 5: AT Commands — Other"""

from utils import *

def test_verify_errors():
    code, j = cmd("AT+VERIFY=nonexistent.bin")
    assert_in(j["response"], "ERROR")

    code, j = cmd("AT+VERIFY=../../etc/passwd")
    assert_in(j["response"], "invalid filename")

def test_forcereset():
    cmd("AT+STATUS")
    code, j = cmd("AT+FORCERESET")
    assert_eq(code, 200)
    assert_eq(j["response"], "OK")

def test_exec_disabled():
    code, j = cmd("AT+EXEC=whoami")
    assert_in(j["response"], "disabled")

if __name__ == "__main__":
    print("\n[5. AT Commands — Other]")
    test("AT+VERIFY errors", test_verify_errors)
    test("AT+FORCERESET", test_forcereset)
    test("AT+EXEC disabled", test_exec_disabled)
    p, f = results(); print(f"  {p}/{p+f} passed")
