#!/usr/bin/env python3
"""Section 1: GET /api/status"""

from utils import *

def run():
    code, j = api("GET", "/api/status")
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")
    assert_in(j, "version")
    assert_in(j, "uptime_seconds")
    assert_in(j, "daemon_busy")
    assert_in(j, "lock_owner")
    print(f"       version={j['version']} uptime={j['uptime_seconds']}s busy={j['daemon_busy']}")

if __name__ == "__main__":
    print("\n[1. Health / Status]")
    test("GET /api/status", run)
    p, f = results(); print(f"  {p}/{p+f} passed")
