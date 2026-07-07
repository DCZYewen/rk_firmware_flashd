# AT Commands — Reference & Implementation

All commands are parsed by `AtCommand::process()` (`src/at_command.cpp:21-57`) which:
1. Trims trailing `\r\n` whitespace
2. Rejects if not starting with `AT` → `"ERROR: commands must start with AT"`
3. Calls `parse_at()` to split `AT+<CMD>=<arg>` into action + argument
4. Dispatches to the appropriate handler

The `parse_at()` function (`src/at_command.cpp:63-83`) uppercases the action so commands are case-insensitive.

---

## AT+STATUS

**File**: `src/at_handler/handler_basic.cpp:11-23`

**Syntax**: `AT+STATUS`

**Response**: `OK uptime=N s serial=open|closed busy=yes|no device=<path>`

**Implementation**:
```
uptime       → steady_clock delta from AtCommand construction
serial       → SerialDaemon::is_open()
busy         → SerialDaemon::is_busy()  (lock != NONE)
device       → serial device path or "none"
```

**Design notes**:
- Uptime resets when `AtCommand` is constructed (once per HTTP request for the HTTP path, once at daemon startup for serial path since the same `AtCommand` instance is reused). This is a minor inconsistency — HTTP gets per-request uptime, serial gets daemon uptime.
- `busy=yes` means the global lock is held by someone, not necessarily by the current caller.

---

## AT+VERSION

**File**: `src/at_handler/handler_version.cpp:20-36`

**Syntax**: `AT+VERSION`

**Response**: `OK daemon=<version> arch=<arch> build=<date> <time>`

**Implementation**:
```
version  → RK_FIRMWARE_VERSION macro (default "0.1.0-dev")
arch     → preprocessor: __x86_64__ / __aarch64__ / __arm__
build    → __DATE__ " " __TIME__  (compile-time string)
```

**Design notes**:
- No runtime CPU detection — arch is baked in at compile time. Cross-compiled binaries will correctly report the target arch, not the host arch.

---

## AT+HELP

**File**: `src/at_handler/handler_basic.cpp:38-54`

**Syntax**: `AT+HELP`

**Response**: `OK AT+STATUS ... AT+HELP` (all commands listed)

**Implementation**: Hardcoded string literal listing every supported command. This must be kept in sync when adding new commands.

---

## AT+RESET

**File**: `src/at_handler/handler_basic.cpp:25-31`

**Syntax**: `AT+RESET`

**Response**: `OK` or `ERROR: only the lock owner can reset`

**Implementation**:
```cpp
if (daemon_.owner() != caller) {
    return "ERROR: only the lock owner can reset";
}
daemon_.release(caller);
return "OK";
```

**Design notes**:
- Only the lock **owner** can release. HTTP commands send `caller=Owner::HTTP`, serial sends `caller=Owner::SERIAL`. If the lock is held by SERIAL, an HTTP `AT+RESET` will be rejected.
- The HTTP handler (`handler_command.cpp:49-50`) treats `AT+RESET` as bypassing the session lock check — it reaches `AtCommand.process()` which then checks owner vs caller and returns the proper error. This avoids HTTP getting stuck thinking it can't even send RESET when SERIAL holds the lock.
- After RESET, the serial port is reopened (the serial daemon's reader loop tries to reconnect on next read).

---

## AT+FORCERESET

**File**: `src/at_handler/handler_basic.cpp:33-36`

**Syntax**: `AT+FORCERESET`

**Response**: `OK`

**Implementation**:
```cpp
daemon_.force_release();
return "OK";
```

**Design notes**:
- Calls `owner_.exchange(Owner::NONE)` — no ownership check. Any caller, any thread, any time.
- Also bypasses the HTTP session lock check in `handler_command.cpp`.
- Purpose: recover from stale sessions where the HTTP client disconnected without sending `AT+RESET`, or the serial device went offline mid-session.

---

## AT+FLASH

**File**: `src/at_handler/handler_flash.cpp:99-149`

**Syntax**: `AT+FLASH=<file>[,<mode>]`

**Modes**:
| Mode | Script |
|------|--------|
| FULL | `flash_full.sh` |
| PARTIAL | `flash_partial.sh` |
| ASSETS | `flash_assets.sh` |

Default mode is `FULL` if omitted.

**Response**:
- Success: `"OK submitted: <scripts_dir>/<script> <file>"`
- Already running: `"ERROR: flash already in progress, use AT+TRYFLASHDONE to poll"`
- Bad mode: `"ERROR: unknown flash mode 'BOGUS' (valid: ASSETS, FULL, PARTIAL)"`
- No filename: `"ERROR: AT+FLASH requires <file>[,MODE]"`

**Implementation**:
1. Parse `filename[,mode]` by splitting on first `,`
2. Look up mode in `FLASH_SCRIPTS` map
3. Check `FlashOp.running` — reject if already running
4. Build command: `scripts_dir + "/" + script + " " + filename`
5. Reset `FlashOp`, launch `std::thread(async_flash_worker, cmd)`
6. Return immediately with `"OK submitted: " + cmd`

**Design notes**:
- `FlashOp` is a function-local static — persists across requests, shared between HTTP and serial
- The thread is `std::thread`, not a thread pool. Only one concurrent flash. A second submit while running returns error.
- `async_flash_worker` uses `popen()` + `pclose()`. The `SIGCHLD` from `pclose()` is handled by the C library's `pclose()` implementation, not by our signal handler.

---

## AT+TRYFLASHDONE

**File**: `src/at_handler/handler_flash.cpp:155-187`

**Syntax**: `AT+TRYFLASHDONE`

**Response**:
- Running: `"RUNNING: <cmd> (N/60s)"`
- Success: `"OK <cmd> <output>"`
- Failure: `"ERROR <cmd> failed (exit=N) <output>"`
- Timeout: `"TIMEOUT: flash did not complete within 60s"`
- No pending: `"ERROR: no pending flash operation"`

**Implementation**:
1. Check `FlashOp.running` and `FlashOp.done` — if neither, return "no pending"
2. If `done == false`:
   - Check elapsed time against `ASYNC_TIMEOUT_SEC` (60s)
   - If exceeded, call `op.cancel()` (joins thread), return TIMEOUT
   - Otherwise return `"RUNNING: ... (N/60s)"`
3. If `done == true`:
   - If exit code 0: `"OK <cmd> <output>"`
   - Else: `"ERROR <cmd> failed (exit=N) <output>"`
   - Join worker thread, reset state

**Design notes**:
- The state is reset after the first poll that sees completion. Subsequent polls return "no pending" — making it idempotent from the caller's perspective.
- `cancel()` joins the thread synchronously. This blocks the HTTP/serial thread for a moment.
- The timeout check uses `steady_clock`, not `system_clock`, so it's immune to system time changes.

---

## AT+EXEC

**File**: `src/at_handler/handler_exec.cpp:10-50`

**Syntax**: `AT+EXEC=<command>`

**Response**:
- Success: `"OK\n<output>"`
- Failure: `"ERROR exit=N\n<output>"`
- Disabled: `"ERROR: AT+EXEC is disabled (rebuild with -DALLOW_RCE to enable)"`

**Implementation**:
```cpp
FILE* pipe = popen(arg.c_str(), "r");
// read stdout into string
int ret = pclose(pipe);
int exit_code = WEXITSTATUS(ret);
```

**Design notes**:
- Gated by `#ifdef ALLOW_RCE` — off by default. Must build with `-DENABLE_RCE=ON` or `build.sh -allow-rce`.
- This is a **security-sensitive** feature. On a production device in the field, RCE should never be enabled. It's intended for development/debugging.
- No command whitelist or sandboxing — the shell interprets the argument directly.

---

## AT+REBOOT

**File**: `src/at_handler/handler_version.cpp:42-64`

**Syntax**: `AT+REBOOT`

**Response**:
- Success: `"OK rebooting"`
- Failure: `"ERROR: reboot failed (exit code N)"`
- Disabled: `"ERROR: AT+REBOOT is disabled (rebuild with -DENABLE_REBOOT=ON)"`

**Implementation**:
```cpp
sync();                          // flush filesystems
system("reboot");                // try reboot first
system("shutdown -r now");       // fallback
```

**Design notes**:
- Gated by `#ifdef ALLOW_REBOOT` — on by default. Disable with `-DENABLE_REBOOT=OFF` or `build.sh --disable-reboot`.
- Two fallback methods: `reboot` (BusyBox/Buildroot) then `shutdown -r now` (systemd/Ubuntu).
- `sync()` is called before reboot to minimize filesystem corruption.

---

## AT+VERIFY

**File**: `src/at_handler/handler_basic.cpp:60-90`

**Syntax**: `AT+VERIFY=<filename>`

**Response**: `"OK <md5hex> <size>"` or `"ERROR: ..."`

**Implementation**:
1. Validate: reject empty, reject `..`, reject `/`
2. Prepend `upload_dir + "/"` to filename — files are only verified from the upload directory
3. `fopen()` → `MD5::update()` in 4KB chunks → `MD5::hex()`
4. Return `"OK <hex> <total_bytes>"`

**Design notes**:
- Files are only accessible from within the upload directory. Path traversal is blocked by the `..` check.
- Error messages don't leak the upload dir path — just the filename argument.
- Uses the same `MD5` implementation as the upload protocol.

---

## AT+PREUPLOAD

**File**: `src/at_handler/handler_upload.cpp:54-112`

**Syntax**: `AT+PREUPLOAD=<filename>,<total_bytes>,<md5_hex>`

**Response**: `"OK READY"` or `"ERROR: ..."`

**Validation**:
- Size must be numeric and > 0
- MD5 must be exactly 32 hex characters
- Filename: max 255 chars, no `/`, no `..`
- No active upload already in progress

**Implementation**:
1. Parse comma-separated arguments
2. Validate all fields
3. Open temp file `upload_<filename>.tmp` in upload dir
4. Initialize `UploadState`: active=true, file handle, md5 context reset
5. Return `"OK READY"`

---

## AT+UPLOAD

**File**: `src/at_handler/handler_upload.cpp:118-200`

**Syntax**: `AT+UPLOAD=<hex_data>,<crc16_hex>,<frame_len>`

**Response**:
- CRC matches: `"OK <total_bytes_received>"`
- CRC mismatch: `"ERROR CRC mismatch frame N expected=XXXX got=XXXX"`
- Other errors: `"ERROR: ..."`

**Implementation**:
1. Parse by splitting on commas from the right (data may contain commas? No — hex data is [0-9a-f] only, so left-to-right split from last two commas)
2. Validate CRC16 hex (exactly 4 hex chars), frame length (numeric)
3. Hex decode data: `sscanf("%2x")` per byte
4. Compute CRC16 over decoded bytes, compare to expected
5. On match: `fwrite()` to temp file, update MD5 context, increment received count
6. On mismatch: increment frame count, return error with expected/computed CRC

**Design notes**:
- CRC16-CCITT with polynomial `0x1021`, init `0xFFFF`. Same algorithm as `include/checksum.h`.
- The error response includes both `expected=` and `got=` CRC so the client can identify which frames need retransmission.
- The declared `frame_len` is compared against actual decoded byte count — mismatch is an error.

---

## AT+UPLOADDONE

**File**: `src/at_handler/handler_upload.cpp:206-257`

**Syntax**: `AT+UPLOADDONE`

**Response**:
- MD5 matches: `"OK <filename> <size> <md5>"`
- MD5 mismatch: `"ERROR MD5 mismatch expected=<md5> got=<md5>"` (temp file deleted)
- Size mismatch: `"ERROR size mismatch expected=N got=N"` (temp file deleted)
- No active upload: `"ERROR: no active upload"`

**Implementation**:
1. Close the temp file
2. Verify received size matches expected size
3. Finalize MD5, compare to expected MD5 from PREUPLOAD
4. On success: rename temp file to final filename
5. On failure: unlink temp file, reset state

**Design notes**:
- The temp file `upload_<filename>.tmp` exists only during transfer. After success it's renamed; after failure it's deleted.
- `rename()` is atomic on the same filesystem — no partial file visible to other processes.
- After success, `UploadState.reset()` clears the state so `UPLOADDONE` returns "no active upload" on next call.

---

## AT+UPLOADCANCEL

**File**: `src/at_handler/handler_upload.cpp:263-281`

**Syntax**: `AT+UPLOADCANCEL`

**Response**: `"OK"` or `"OK no upload in progress"`

**Implementation**:
1. Close file handle if open
2. Unlink temp file
3. Reset upload state
4. Return OK

**Design notes**:
- No-op if no upload is active — returns OK regardless. This is intentional: the client can always cancel to reset the upload state without checking status first.
