# HTTP Endpoints — Reference & Implementation

## Common Behavior

- All JSON responses include CORS header `Access-Control-Allow-Origin: *`
- Unknown URL/method combos return `404 {"status":"error","message":"Not found"}`
- POST body accumulation uses `ConnectionState.cmd_body` (for `/api/command`) or `MHD_post_processor` (for `/api/upload`)
- Connection timeout: 120 seconds (`MHD_OPTION_CONNECTION_TIMEOUT`)

---

## GET /api/status

**File**: `src/http_handler/handler_status.cpp:8-36`

**Purpose**: Daemon health check and lock state inspection. Used by monitoring tools and test scripts.

**Response `200`**:
```json
{
  "status": "ok",
  "version": "0.1.0",
  "uptime_seconds": 253,
  "serial_port": "/dev/ttyS1",
  "serial_connected": true,
  "daemon_busy": false,
  "lock_owner": null,
  "port": 8080,
  "upload_dir": "/home/cat"
}
```

**Implementation**:
- `uptime_seconds`: `steady_clock::now() - start_time_` (HttpServer construction time)
- `serial_port`: from `Config.serial_device`
- `serial_connected`: `SerialDaemon::is_open()`
- `daemon_busy`: `SerialDaemon::is_busy()` — true if lock is held by anyone
- `lock_owner`: `"HTTP"` or `"SERIAL"` string, or `null` (JSON `null`, not `"null"`)
- `upload_dir`: from `Config.upload_dir` — used by test scripts to find uploaded files

**Design notes**:
- The `upload_dir` field is used by `real_test/utils.py` to auto-detect where uploaded files land — no need to pass it as an env var.
- `lock_owner` uses `nlohmann::json` with `nullptr` for the null case, producing a bare JSON `null` rather than a string.

---

## POST /api/upload

**File**: `src/http_handler/handler_upload.cpp:11-100`

**Purpose**: Upload firmware file via `multipart/form-data`.

**Request**: `multipart/form-data` with field name `firmware`:
```http
Content-Type: multipart/form-data; boundary=----Boundary7MA4YWxkTrZu0gW

------Boundary7MA4YWxkTrZu0gW
Content-Disposition: form-data; name="firmware"; filename="test_fw.bin"
Content-Type: application/octet-stream

<binary data>
------Boundary7MA4YWxkTrZu0gW--
```

**Response `200`**:
```json
{
  "status": "ok",
  "filename": "test_fw.bin",
  "path": "/tmp/rk_flashd_uploads/test_fw.bin",
  "size": 16384
}
```

**Response `400`** (rejected filename):
```json
{
  "status": "error",
  "message": "Upload failed: rejected filename '../../etc/passwd'"
}
```

**Response `400`** (wrong field name):
```json
{
  "status": "error",
  "message": "Upload failed: unexpected field 'wrongfield' (expected 'firmware')"
}
```

**Implementation**:
1. `accessHandler` creates a `MHD_create_post_processor` for `/api/upload` POST requests
2. The post processor callback handles each multipart field:
   - Rejects fields not named `firmware` with descriptive error
   - Validates filename via `validate_filename_` lambda (rejects `/`, `\`, `..`, empty, non-alphanumeric)
   - Opens temp file `<timestamp>_<filename>` in upload dir
   - Writes data chunks as they arrive
3. `handleUpload()` finalizes:
   - Closes temp file
   - Validates filename (second check — defense in depth)
   - Renames temp file to `upload_dir/filename`
   - Returns JSON with path and size

**Design notes**:
- The temp filename includes a timestamp to avoid collisions if multiple uploads happen simultaneously — though in practice MHD handles one connection at a time per thread.
- Filename validation runs twice: once in the post-processor callback (rejects before writing), and once in `handleUpload` (defense in depth before rename).
- The error message includes the specific reason (rejected filename, wrong field) to aid debugging.
- CORS headers are set on error responses too — the `sendJson()` helper adds them.

---

## POST /api/command

**File**: `src/http_handler/handler_command.cpp:8-90`

**Purpose**: Execute an AT command inside an HTTP session. The session lock is acquired on first command and held until `AT+RESET`.

**Request**:
```json
{
  "cmd": "AT+STATUS"
}
```

**Response `200`**:
```json
{
  "status": "ok",
  "response": "OK uptime=0s serial=closed busy=yes device=none"
}
```

**Response `503`** (serial holds lock):
```json
{
  "status": "busy",
  "message": "serial daemon is busy, wait for AT+RESET"
}
```

**Response `400`** (bad JSON):
```json
{
  "status": "error",
  "message": "Invalid JSON: <parse error details>"
}
```

**Response `400`** (missing cmd):
```json
{
  "status": "error",
  "message": "Missing 'cmd' field"
}
```

**Implementation**:
1. Body accumulation: MHD may deliver POST data in chunks. The handler accumulates into `state->cmd_body` until `*upload_data_size == 0` signals completion.
2. JSON parse: `nlohmann::json::parse(state->cmd_body)`. Catch `parse_error` for bad JSON.
3. Validate `cmd` field: must be a string.
4. Session lock logic:
   - `AT+FORCERESET` and `AT+RESET` bypass the lock check (always reach `AtCommand`)
   - If lock is free → `try_acquire(Owner::HTTP)` — may race with serial, returns 503 if serial wins
   - If HTTP already owns → proceed (session reuse)
   - If SERIAL owns → 503 BUSY
5. `AtCommand::process(cmd, Owner::HTTP)` → returns response string
6. Return `{"status":"ok","response":"..."}`

**Design notes**:
- The bypass for `AT+RESET` and `AT+FORCERESET` is critical: without it, if SERIAL holds the lock, HTTP can't even send RESET to recover. These commands do their own ownership check inside `AtCommand`.
- The lock is intentionally NOT released after each command response. It persists until `AT+RESET` is processed (which calls `daemon.release(HTTP)` inside `handle_reset`).
- Error responses use `nlohmann::json` for proper JSON escaping (bug 006 fix: previously used raw string interpolation which broke on special characters).

---

## POST /api/reset

**File**: `src/http_handler/handler_reset.cpp:7-18`

**Purpose**: Force-release the session lock. Administrative override — always succeeds.

**Response `200`**:
```json
{
  "status": "ok"
}
```

**Implementation**:
```cpp
server->daemon().force_release();
// return {"status":"ok"}
```

**Design notes**:
- Calls `owner_.exchange(Owner::NONE)` — the atomic exchange is thread-safe and lock-free.
- No ownership check. This is the "break glass in case of emergency" endpoint.
- Use this when a client holds the lock but can't (or won't) send `AT+RESET` — e.g. browser closed, curl interrupted, network failure.

---

## POST /api/debug/lock-serial (Debug Only)

**File**: `src/http_server.cpp:179-192`

**Purpose**: Simulate a serial-side lock for testing cross-owner scenarios without a physical serial device.

**Response `200`**:
```json
{
  "status": "ok",
  "message": "Lock acquired for SERIAL"
}
```

**Response `409`** (lock held):
```json
{
  "status": "error",
  "message": "Lock is already held"
}
```

**Implementation**: Compiled only in `!NDEBUG` (Debug builds). Calls `try_acquire(Owner::SERIAL)` directly.

**Design notes**:
- Only available in Debug builds — excluded from Release. This is enforced by `#ifndef NDEBUG` in `src/http_server.cpp`.
- Used by `real_test/test_11_debug_lock.py` to test cross-owner behavior:
  1. Acquire SERIAL via debug endpoint
  2. Verify HTTP receives "only the lock owner can reset" on AT+RESET
  3. Verify status shows `lock_owner: "SERIAL"`
  4. Verify AT+FORCERESET still works from HTTP
  5. Force-release and verify lock is free
