#!/bin/bash
# Manual API test script for rk_firmware_flashd
# Usage: bash test/test_webapi.sh

BASE="http://127.0.0.1:8080"

echo "=== GET /api/status ==="
curl -s "$BASE/api/status" | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+STATUS) ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+STATUS"}' | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+HELP) ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+HELP"}' | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+EXEC=whoami) — should be disabled ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+EXEC=whoami"}' | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+FLASH=test.bin) — will fail but shows param passing ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+FLASH=test.bin"}' | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+FLASH) — no args ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+FLASH"}' | python3 -m json.tool
echo ""

echo "=== POST /api/upload ==="
dd if=/dev/urandom of=/tmp/test_fw.bin bs=1024 count=16 2>/dev/null
echo "Created $(stat -c%s /tmp/test_fw.bin) byte test file"
curl -s -X POST "$BASE/api/upload" \
  -F "firmware=@/tmp/test_fw.bin;filename=test_fw.bin" | python3 -m json.tool
rm -f /tmp/test_fw.bin
echo ""

echo "=== POST /api/command (AT+UPLOADDONE) — no upload active ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+UPLOADDONE"}' | python3 -m json.tool
echo ""

echo "=== POST /api/reset ==="
curl -s -X POST "$BASE/api/reset" | python3 -m json.tool
echo ""

echo "=== POST /api/command (AT+STATUS after reset) ==="
curl -s -X POST "$BASE/api/command" \
  -H 'Content-Type: application/json' \
  -d '{"cmd":"AT+STATUS"}' | python3 -m json.tool
echo ""

