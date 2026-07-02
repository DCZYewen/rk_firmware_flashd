#!/usr/bin/env python3
"""Section 6: AT Commands — Error Edge Cases"""

from utils import *

def test_bad_json():
    code, j = api("POST", "/api/command", raw_body=b"not json")
    assert_eq(j.get("status"), "error")

def test_missing_cmd():
    code, j = api("POST", "/api/command", {"foo": "bar"})
    assert_in(j.get("message", ""), "cmd")

def test_empty_json():
    code, j = api("POST", "/api/command", {})
    assert_in(j.get("message", ""), "cmd")

def test_unknown_at():
    code, j = cmd("AT+BOGUS")
    assert_in(j["response"], "unknown command")

if __name__ == "__main__":
    print("\n[6. AT Commands — Error Edge Cases]")
    test("Bad JSON", test_bad_json)
    test("Missing cmd field", test_missing_cmd)
    test("Empty JSON", test_empty_json)
    test("Unknown AT command", test_unknown_at)
    p, f = results(); print(f"  {p}/{p+f} passed")
