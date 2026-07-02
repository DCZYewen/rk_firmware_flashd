#!/usr/bin/env python3
"""Run all section tests and print summary."""

import subprocess
import sys
import os

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://10.0.10.101:8080"

scripts = [
    "test_01_health.py",
    "test_02_upload_http.py",
    "test_03_at_basic.py",
    "test_04_flash.py",
    "test_05_at_other.py",
    "test_06_at_errors.py",
    "test_07_session_lock.py",
    "test_08_force_reset.py",
    "test_09_upload_protocol.py",
    "test_11_debug_lock.py",
]

total_passed = 0
total_failed = 0
dir = os.path.dirname(os.path.abspath(__file__))

for script in scripts:
    path = os.path.join(dir, script)
    ret = subprocess.run([sys.executable, path, BASE])
    if ret.returncode == 0:
        total_passed += 1
    else:
        total_failed += 1

print(f"\n{'='*50}")
print(f"  {total_passed}/{len(scripts)} sections passed, {total_failed} failed")
if total_failed:
    sys.exit(1)
