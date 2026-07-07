# rk_firmware_flashd

C++ daemon that exposes HTTP APIs for firmware upload and AT-style command execution on Rockchip devices. Uses `libmicrohttpd` for HTTP and `libserialport` (with POSIX fallback) for UART serial communication.

## Quick Start

```bash
# Dependencies (Ubuntu/Debian)
sudo apt install cmake g++ libmicrohttpd-dev libserialport-dev pkg-config

# Build
bash build.sh                    # Debug
bash build.sh Release            # Release
bash build.sh -allow-rce         # Enable AT+EXEC (shell commands)
bash build.sh --cross-toolchain=/path/to/aarch64-gcc  # Cross-compile

# Run
./build/rk_firmware_flashd --port 8080 --foreground

# Test
ctest --test-dir build           # C++ unit tests
python3 real_test/run_all.py build/rk_firmware_flashd  # Integration
```

## Architecture

A unified `AtCommand` processor handles both HTTP and serial paths. A global `std::atomic<Owner>` lock prevents concurrent access — the first caller (HTTP or serial) acquires it and holds it across multiple commands until `AT+RESET`. Flash operations run asynchronously via `std::thread` + `popen()`; polling with `AT+TRYFLASHDONE` (60s timeout). Upload protocol uses CRC16 per frame and MD5 for whole-file integrity.

### Endpoints

| Method | Path | Purpose |
|--------|------|---------|
| POST | `/api/upload` | Upload firmware (multipart) |
| POST | `/api/command` | Execute AT command |
| GET | `/api/status` | Daemon health + lock state |
| POST | `/api/reset` | Force-release lock |

### AT Commands

`AT+STATUS`, `AT+RESET`, `AT+FORCERESET`, `AT+HELP`, `AT+VERSION`, `AT+FLASH` (async), `AT+TRYFLASHDONE`, `AT+EXEC` (rce), `AT+REBOOT`, `AT+VERIFY`, `AT+PREUPLOAD`, `AT+UPLOAD`, `AT+UPLOADDONE`, `AT+UPLOADCANCEL`

## Project Layout

```
src/               — Daemon, server, serial, commands
include/           — Headers
test/              — C++ unit tests (CTest)
real_test/         — Python integration tests
3rd_party/         — nlohmann/json, libmicrohttpd, libserialport
cmake/toolchain/   — Cross-compilation toolchain files
docs/              — Developer documentation (AT commands, HTTP API, architecture)
project/           — API docs, bug history, design plan
```

See `docs/at_commands.md` for AT command reference with implementation notes,
`docs/http_endpoints.md` for HTTP endpoint reference with implementation notes,
`docs/architecture.md` for the overall design and thread model.
