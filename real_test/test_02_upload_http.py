#!/usr/bin/env python3
"""Section 2: HTTP Multipart Upload — verifies file lands on disk."""

import os
from utils import *

def test_valid():
    data = os.urandom(16384)
    code, j = api("POST", "/api/upload", files=[("firmware", "test_fw.bin", data)])
    assert_eq(code, 200)
    assert_eq(j["status"], "ok")
    assert_eq(j["filename"], "test_fw.bin")
    assert_eq(j["size"], 16384)

    path = os.path.join(UPLOAD_DIR, "test_fw.bin")
    assert os.path.exists(path), f"file not found at {path}"
    with open(path, "rb") as f:
        assert_eq(f.read(), data, "content mismatch")
    os.remove(path)
    print(f"       verified {j['size']} bytes at {path}")

def test_path_traversal():
    code, j = api("POST", "/api/upload", files=[("firmware", "../../etc/passwd", b"x")])
    assert_eq(code, 400)

def test_wrong_field():
    code, j = api("POST", "/api/upload", files=[("wrongfield", "x.txt", b"x")])
    assert_eq(code, 400)

if __name__ == "__main__":
    print("\n[2. Upload Firmware — HTTP Multipart]")
    test("Upload valid 16KB file", test_valid)
    test("Upload path traversal rejected", test_path_traversal)
    test("Upload wrong field name", test_wrong_field)
    p, f = results(); print(f"  {p}/{p+f} passed")
