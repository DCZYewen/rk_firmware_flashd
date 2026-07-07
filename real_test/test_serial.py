#!/usr/bin/env python3
"""
Serial AT command integration tests.
Creates a virtual serial pair with socat, starts the daemon, and tests AT commands.

Usage:  python3 test_serial.py [daemon_binary_path]
"""

import os
import sys
import time
import subprocess
import threading
import atexit

try:
    import serial as pyserial
except ImportError:
    print("pyserial not installed. Run: pip3 install pyserial")
    sys.exit(1)

DAEMON_BIN = sys.argv[1] if len(sys.argv) > 1 else ""

# ---------------------------------------------------------------------------
# Lifecycle
# ---------------------------------------------------------------------------
SOCAT = None
DAEMON = None
SERIAL = None
TTY_CLIENT = None

def setup():
    global SOCAT, DAEMON, SERIAL, TTY_CLIENT

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    # Kill leftovers
    os.system("pkill socat 2>/dev/null")
    os.system("pkill rk_firmware_flashd 2>/dev/null")
    time.sleep(0.5)

    # Resolve daemon binary
    daemon_bin = DAEMON_BIN or os.path.join(project_dir, "build", "rk_firmware_flashd")
    if not os.path.exists(daemon_bin):
        print(f"Daemon not found at {daemon_bin}")
        sys.exit(1)

    # Socat
    print("Starting socat...", flush=True)
    SOCAT = subprocess.Popen(
        ["socat", "-d", "-d", "PTY,link=/tmp/tty_flashd,rawer,echo=0",
                                "PTY,link=/tmp/tty_client,rawer,echo=0"],
        stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
    time.sleep(1)

    if SOCAT.poll() is not None:
        print("socat failed to start", flush=True)
        cleanup()
        sys.exit(1)

    TTY_CLIENT = os.path.realpath("/tmp/tty_client")
    tty_daemon = os.path.realpath("/tmp/tty_flashd")
    print(f"  socat: {tty_daemon} <-> {TTY_CLIENT}", flush=True)

    # Daemon (with POSIX fallback for PTY)
    print("Starting daemon...", flush=True)
    DAEMON = subprocess.Popen([
        daemon_bin, "--foreground", "--port", "8181",
        "--serial-device", tty_daemon, "--baud-rate", "115200",
        "--scripts-dir", os.path.join(project_dir, "script"),
        "--upload-dir", "/tmp",
    ])
    time.sleep(1)

    if DAEMON.poll() is not None:
        print("Daemon failed to start", flush=True)
        cleanup()
        sys.exit(1)
    print(f"  daemon PID {DAEMON.pid}", flush=True)

    # Serial client
    SERIAL = pyserial.Serial(TTY_CLIENT, 115200, timeout=5)
    print(f"  serial client on {TTY_CLIENT}", flush=True)

def cleanup():
    if SERIAL and SERIAL.is_open:
        SERIAL.close()
    if DAEMON:
        DAEMON.terminate()
        try: DAEMON.wait(3)
        except: pass
    if SOCAT:
        SOCAT.terminate()
        try: SOCAT.wait(2)
        except: pass

atexit.register(cleanup)

# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------
LOCK = threading.Lock()
_last_req = ""
_last_resp = ""

def scmd(at_cmd):
    global _last_req, _last_resp
    _last_req = at_cmd
    with LOCK:
        SERIAL.reset_input_buffer()
        SERIAL.write((at_cmd + "\r\n").encode())
        _last_resp = SERIAL.readline().decode(errors="replace").strip()
    return _last_resp

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000: crc = (crc << 1) ^ 0x1021
            else: crc <<= 1
        crc &= 0xFFFF
    return crc

import hashlib
def md5_hex(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()

passed = 0
failed = 0

def test(name, fn):
    global passed, failed
    try:
        fn()
        print(f"  PASS  {name}", flush=True)
        passed += 1
    except Exception as e:
        print(f"  FAIL  {name}", flush=True)
        print(f"    CMD: {_last_req}", flush=True)
        print(f"    RSP: {_last_resp}", flush=True)
        print(f"    ERR: {e}", flush=True)
        failed += 1

def assert_eq(a, b, m=""):
    if a != b:
        raise AssertionError(f"expected {b!r}, got {a!r}" + (f" ({m})" if m else ""))

def assert_in(h, n, m=""):
    if n not in h:
        raise AssertionError(f"expected {n!r} in {h!r}" + (f" ({m})" if m else ""))

# ===================================================================
# TESTS
# ===================================================================

def test_status():
    r = scmd("AT+STATUS")
    assert_in(r, "OK")
    assert_in(r, "uptime=")

def test_version():
    r = scmd("AT+VERSION")
    assert_in(r, "OK daemon=")

def test_help():
    r = scmd("AT+HELP")
    assert_in(r, "AT+FLASH")

def test_unknown():
    r = scmd("AT+BOGUS")
    assert_in(r, "unknown command")

def test_not_at():
    r = scmd("HELLO")
    assert_in(r, "must start with AT")

def test_upload_single():
    data = b"hello"
    m = md5_hex(data)
    c = crc16(data)

    r = scmd(f"AT+PREUPLOAD=ser_hello.txt,{len(data)},{m}")
    assert_eq(r, "OK READY")

    r = scmd(f"AT+UPLOAD={data.hex()},{c:04X},{len(data)}")
    assert_in(r, "OK 5")

    r = scmd("AT+UPLOADDONE")
    assert_in(r, "OK ser_hello.txt")
    assert_in(r, m)

    os.remove("/tmp/ser_hello.txt")

# ===================================================================
print()
setup()

print("\n[Serial — Basic AT]", flush=True)
test("AT+STATUS", test_status)
test("AT+VERSION", test_version)
test("AT+HELP", test_help)
test("Unknown command", test_unknown)
test("Non-AT command", test_not_at)

print("\n[Serial — Upload Protocol]", flush=True)
test("PREUPLOAD → UPLOAD → UPLOADDONE", test_upload_single)

total = passed + failed
print(f"\n  {passed}/{total} passed, {failed} failed", flush=True)
if failed:
    sys.exit(1)
