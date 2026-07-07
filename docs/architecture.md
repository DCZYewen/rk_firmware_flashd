# Architecture — rk_firmware_flashd

## Design Philosophy

The daemon is a single-process, multi-threaded C++14 application. All AT commands are handled **locally** — nothing is forwarded to the device firmware. The daemon manages the serial port, the HTTP server, and the global session lock. Flash scripts are shell scripts spawned via `popen()`.

Key constraints:
- No external dependencies beyond `libmicrohttpd` + `libserialport` (both optional at runtime)
- Self-contained checksum code (`include/checksum.h`) — CRC16-CCITT + MD5, no OpenSSL
- Must work on Buildroot (minimal libc, no syslog)
- POSIX fallback for serial when libserialport can't handle PTY devices

---

## Thread Model

```
Main thread:
  main() → parse_args → daemonize → init_logger → setup_signals
         → SerialDaemon.start() (maybe opens serial port)
         → SerialDaemon.start_reader() (launches reader thread)
         → HttpServer.start() (launches MHD internal threads)
         → while(running) sleep(1)

Reader thread (SerialDaemon):
  loop:
    readLine(500ms timeout) from serial device
    try_acquire(Owner::SERIAL) or reuse if SERIAL already owns
    → AtCommand.process(line, Owner::SERIAL)
    → writeLine(response) back to serial

Flash worker thread (per flash op):
  popen(flash_script + filename, "r")
  read stdout until EOF
  pclose → capture exit code
  set FlashOp.done = true

MHD internal threads (libmicrohttpd):
  handle HTTP connections, dispatch to handler functions
  POST /api/command → try_acquire/reuse Owner::HTTP, then AtCommand.process()
```

The global lock (`std::atomic<Owner>`) is the only shared mutable state between threads. All serial I/O is under `io_mutex_`.

---

## Global Lock (`serial_daemon.cpp:52-92`)

```cpp
enum class Owner { NONE, HTTP, SERIAL };
std::atomic<Owner> owner_{Owner::NONE};
```

- `try_acquire(who)` — CAS `NONE → who`, returns false if already held
- `release(who)` — CAS `who → NONE`, only owner can release
- `force_release()` — `exchange(NONE)`, any thread can call (admin override)
- `is_busy()` — `load() != NONE`
- `owner()` — `load()`

Why atomic CAS instead of mutex: the lock is checked on every HTTP request and every serial command. A blocking mutex would stall the MHD worker threads. CAS is wait-free for the common case (lock is free).

Why HTTP holds lock across requests (session model): firmware upload requires multiple sequential AT commands (`PREUPLOAD → UPLOAD × N → UPLOADDONE → FLASH`). If the lock were released between HTTP requests, serial-side commands could interleave mid-sequence.

### Lock reuse

Both HTTP and serial check "do I already own the lock?" before attempting CAS:

**HTTP** (`handler_command.cpp:53-71`):
```cpp
if (!daemon.is_busy()) {
    try_acquire(HTTP);  // NONE → HTTP
} else if (daemon.owner() != HTTP) {
    return 503 BUSY;    // SERIAL holds it
}
// HTTP already owns → proceed
```

**Serial** (`serial_daemon.cpp:162-170`):
```cpp
if (!try_acquire(SERIAL)) {
    if (owner() != SERIAL) {
        writeLine("BUSY");  // HTTP holds it
        continue;
    }
    // SERIAL already owns → proceed
}
```

This was bug 007 — originally the serial reader always tried CAS, so after the first serial command succeeded, every subsequent serial command got BUSY.

---

## Serial Port (`src/serial_port.cpp`)

Two-layer open strategy:

1. **libserialport** (primary): `sp_get_port_by_name` + `sp_open` — works on real hardware (ttyS0, ttyUSB0) where the device supports the probe ioctls
2. **POSIX fallback** (secondary): `open(O_RDWR | O_NOCTTY)` + `tcsetattr()` — used when libserialport fails (e.g. PTY devices from socat, or unusual tty drivers)

Both paths share the same `writeLine()`/`readLine()` interface. The POSIX fallback uses raw mode (`c_lflag = 0`, `VMIN=0, VTIME=1` for 100ms timeout).

`readLine()` accumulates bytes until `\n`, strips trailing `\r`, with an 8192-byte line limit. On timeout (configurable, default 500ms in reader loop), returns false — the reader loop tries to reconnect.

---

## Async Flash (`src/at_handler/handler_flash.cpp`)

```cpp
struct FlashOp {
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> done{false};
    int exit_code;
    std::string output;
    std::string cmd;
    std::chrono::steady_clock::time_point start_time;
};
```

State is a function-local static (`flash_op()`). Only one concurrent flash — `AT+FLASH` while `running == true` returns an error.

The background thread runs `popen(cmd, "r")`, reads stdout line by line, then `pclose()` to get exit code. On success (exit=0), `AT+TRYFLASHDONE` returns `"OK <cmd> <output>"`. On failure, returns `"ERROR <cmd> failed (exit=N) <output>"`.

Timeout after 60 seconds calls `cancel()` which joins the thread and returns `"TIMEOUT"`.

Why `popen()` instead of `fork()`/`exec()`: we originally used fork+pipe but `SIGCHLD=SIG_IGN` (set by libmicrohttpd) broke `waitpid()`. `popen()` handles SIGCHLD internally. Bug 004.

---

## Upload Protocol (`src/at_handler/handler_upload.cpp`)

State is a function-local static `UploadState` — same pattern as `FlashOp`. This persists across HTTP requests within a session (since a new `AtCommand` is created per HTTP request).

Integrity:
- **Per-frame**: CRC16-CCITT (polynomial 0x1021, init 0xFFFF). The client sends hex data + CRC + declared length. The daemon recomputes CRC and rejects mismatches.
- **Whole-file**: MD5 sent in `PREUPLOAD`, verified in `UPLOADDONE`. Computed incrementally during upload (no second pass).

The temp file `upload_<filename>.tmp` is written during upload and renamed to `<filename>` on successful `UPLOADDONE`. On CRC error, MD5 mismatch, or `UPLOADCANCEL`, the temp file is deleted.

---

## HTTP Server (`src/http_server.cpp`)

Uses `libmicrohttpd` with `MHD_USE_INTERNAL_POLLING_THREAD` (single internal thread pool). Routes:

| URL | Method | Handler | State type |
|-----|--------|---------|------------|
| `/api/upload` | POST | `handleUpload` | `UPLOAD` with post processor |
| `/api/command` | POST | `handleCommand` | `COMMAND`, body accumulated in `cmd_body` |
| `/api/status` | GET | `handleStatus` | `STATUS`, immediate response |
| `/api/reset` | POST | `handleReset` | `RESET`, immediate response |
| `/api/debug/lock-serial` | POST | immediate in accessHandler | Debug only (`!NDEBUG`) |

CORS headers (`Access-Control-Allow-Origin: *`) are set on all JSON responses for browser-based clients.

`ConnectionState` is allocated per-connection and destroyed by MHD after the response is sent. The destructor handles cleanup: destroys post processor, closes temp file, removes incomplete uploads.

---

## Daemon Lifecycle (`main.cpp`)

1. Parse CLI args (`--port`, `--serial-device`, `--upload-dir`, etc.)
2. Resolve relative paths (before daemonize changes cwd to `/`)
3. Create upload directory (`mkdir -p`)
4. Daemonize (double-fork + setsid) unless `--foreground`
5. Init logger (file or stderr)
6. `setup_signals()` — `SIGTERM`/`SIGINT` set `running=false`, `SIGCHLD` handled for popen reaping
7. Start `SerialDaemon` (open serial port if configured)
8. Create `AtCommand` (shared by HTTP and serial)
9. Start serial reader thread (if serial configured)
10. Start HTTP server
11. Main loop: `sleep(1)` until shutdown signal
12. Cleanup: stop HTTP, stop reader, stop serial, remove PID file
