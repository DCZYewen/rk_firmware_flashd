#!/usr/bin/env python3
"""Section 9: Firmware Upload Protocol — each step is a separate test."""

import os
from utils import *

def test_preupload_single():
    data = b"hello"
    m = md5_hex(data)
    code, j = cmd(f"AT+PREUPLOAD=hello_up.txt,{len(data)},{m}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK READY")

def test_upload_frame():
    data = b"hello"
    c = crc16(data)
    code, j = cmd(f"AT+UPLOAD={data.hex()},{c:04X},{len(data)}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK 5")

def test_uploaddone_single():
    data = b"hello"
    m = md5_hex(data)
    code, j = cmd("AT+UPLOADDONE")
    assert_eq(code, 200)
    assert_in(j["response"], "OK hello_up.txt")
    assert_in(j["response"], m)
    path = os.path.join(UPLOAD_DIR, "hello_up.txt")
    assert os.path.exists(path)
    with open(path, "rb") as f:
        assert_eq(f.read(), data)
    os.remove(path)

def test_preupload_multi():
    data = bytes(range(16))
    m = md5_hex(data)
    code, j = cmd(f"AT+PREUPLOAD=data_up.bin,{len(data)},{m}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK READY")

def test_upload_frame1():
    chunk = bytes(range(8))
    c = crc16(chunk)
    code, j = cmd(f"AT+UPLOAD={chunk.hex()},{c:04X},{len(chunk)}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK 8")

def test_upload_frame2():
    chunk = bytes(range(8, 14))
    c = crc16(chunk)
    code, j = cmd(f"AT+UPLOAD={chunk.hex()},{c:04X},{len(chunk)}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK 14")

def test_upload_frame3():
    chunk = bytes(range(14, 16))
    c = crc16(chunk)
    code, j = cmd(f"AT+UPLOAD={chunk.hex()},{c:04X},{len(chunk)}")
    assert_eq(code, 200)
    assert_in(j["response"], "OK 16")

def test_uploaddone_multi():
    data = bytes(range(16))
    m = md5_hex(data)
    code, j = cmd("AT+UPLOADDONE")
    assert_eq(code, 200)
    assert_in(j["response"], f"OK data_up.bin {len(data)} {m}")
    path = os.path.join(UPLOAD_DIR, "data_up.bin")
    assert os.path.exists(path)
    with open(path, "rb") as f:
        assert_eq(f.read(), data)
    os.remove(path)

def test_uploaddone_no_active():
    code, j = cmd("AT+UPLOADDONE")
    assert_in(j["response"], "no active upload")

def test_preupload_bad_md5():
    code, j = cmd("AT+PREUPLOAD=f.bin,1,zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz")
    assert_in(j["response"], "invalid md5 hex")

def test_preupload_zero_size():
    code, j = cmd("AT+PREUPLOAD=f.bin,0,d41d8cd98f00b204e9800998ecf8427e")
    assert_in(j["response"], "size must be > 0")

def test_preupload_path_traversal():
    code, j = cmd("AT+PREUPLOAD=../../etc/passwd,1,d41d8cd98f00b204e9800998ecf8427e")
    assert_in(j["response"], "filename contains /")

if __name__ == "__main__":
    print("\n[9. Firmware Upload Protocol]")

    print("   -- Single frame --")
    test("PREUPLOAD hello", test_preupload_single)
    test("UPLOAD frame", test_upload_frame)
    test("UPLOADDONE + verify file", test_uploaddone_single)

    print("   -- Multi-frame (3 chunks) --")
    test("PREUPLOAD 16 bytes", test_preupload_multi)
    test("Frame 1 (bytes 0-7)", test_upload_frame1)
    test("Frame 2 (bytes 8-13)", test_upload_frame2)
    test("Frame 3 (bytes 14-15)", test_upload_frame3)
    test("UPLOADDONE + verify file", test_uploaddone_multi)

    print("   -- Error paths --")
    test("UPLOADDONE no active", test_uploaddone_no_active)
    test("PREUPLOAD bad MD5 hex", test_preupload_bad_md5)
    test("PREUPLOAD zero size", test_preupload_zero_size)
    test("PREUPLOAD path traversal", test_preupload_path_traversal)

    p, f = results()
    print(f"  {p}/{p+f} passed")
