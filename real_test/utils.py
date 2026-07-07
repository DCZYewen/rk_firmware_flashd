"""Shared helpers for all section tests."""

import json
import hashlib
import urllib.request
import urllib.error
import sys
import os
import traceback

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"

# Detect upload dir from daemon /api/status, fallback to env var or home
try:
    url = BASE + "/api/status"
    with urllib.request.urlopen(url, timeout=5) as resp:
        _status = json.loads(resp.read())
    UPLOAD_DIR = _status.get("upload_dir", os.environ.get("UPLOAD_DIR", os.path.expanduser("~")))
except Exception:
    UPLOAD_DIR = os.environ.get("UPLOAD_DIR", os.path.expanduser("~"))

passed = 0
failed = 0
_last_req = None
_last_resp = None

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
        crc &= 0xFFFF
    return crc

def md5_hex(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()

def api(method, path, body=None, files=None, raw_body=None, timeout=30):
    global _last_req, _last_resp
    url = BASE + path
    if files:
        boundary = "----Boundary7MA4YWxkTrZu0gW"
        data = b""
        for name, filename, content in files:
            data += f"--{boundary}\r\n".encode()
            data += f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'.encode()
            data += b"Content-Type: application/octet-stream\r\n\r\n"
            data += content + b"\r\n"
        data += f"--{boundary}--\r\n".encode()
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
        _last_req = f"{method} {path} [multipart, {len(data)} bytes]"
    elif raw_body is not None:
        req = urllib.request.Request(url, data=raw_body, method=method)
        req.add_header("Content-Type", "application/json")
        _last_req = f"{method} {path} {raw_body}"
    elif body is not None:
        data = json.dumps(body).encode()
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Content-Type", "application/json")
        _last_req = f"{method} {path} {body}"
    else:
        req = urllib.request.Request(url, method=method)
        _last_req = f"{method} {path}"
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read()
            _last_resp = (resp.status, body)
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read()
        _last_resp = (e.code, body)
        return e.code, json.loads(body)

def cmd(at_cmd):
    return api("POST", "/api/command", {"cmd": at_cmd})

def test(name, fn):
    global passed, failed
    try:
        fn()
        passed += 1
    except (AssertionError, Exception) as e:
        failed += 1
        print(f"  FAIL  {name}", flush=True)
        print(f"    REQ: {_last_req}", flush=True)
        if _last_resp:
            try:
                resp_json = json.loads(_last_resp[1])
                print(f"    RSP: [{_last_resp[0]}] {json.dumps(resp_json, indent=6)}", flush=True)
            except json.JSONDecodeError:
                print(f"    RSP: [{_last_resp[0]}] {_last_resp[1][:200]}", flush=True)
        print(f"    ERR: {e}", flush=True)
        return
    if _last_req:
        rsp = ""
        if _last_resp:
            try:
                rsp = json.dumps(json.loads(_last_resp[1]))
            except json.JSONDecodeError:
                rsp = str(_last_resp[1][:200])
        print(f"  PASS  {name}", flush=True)
        print(f"    REQ: {_last_req}", flush=True)
        print(f"    RSP: {rsp}", flush=True)
    else:
        print(f"  PASS  {name}", flush=True)

def assert_eq(a, b, msg=""):
    if a != b:
        raise AssertionError(f"expected {b!r}, got {a!r}" + (f" ({msg})" if msg else ""))

def assert_in(haystack, needle, msg=""):
    if needle not in haystack:
        raise AssertionError(f"expected {needle!r} in {haystack!r}" + (f" ({msg})" if msg else ""))

def results():
    return passed, failed
