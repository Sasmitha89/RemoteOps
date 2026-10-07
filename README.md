# RemoteOps: Remote System Monitoring and Management Tool over TCP/IP

**Module:** IE3090 Network Programming (Year 3, Semester 1)
**Student registration number:** IT24102290
**Language:** C, BSD sockets (`sys/socket.h`), POSIX threads
**Platform:** Linux (developed and tested on CentOS, compiled with `gcc`)

RemoteOps consists of an **Agent** (the server, running on the managed machine) and a **Controller** (the client, used by an administrator). They communicate over a fixed, line-based text protocol on TCP, with a secondary UDP channel for periodic system monitoring.

---

## 1. Personalised values (derived from IT24102290)

| Item | Formula | Value |
|---|---|---|
| Registration number | n/a | `IT24102290` |
| Numeric part | registration number without `IT` | `24102290` |
| Agent listening port | 7000 + first four digits (`2410`) | **9410** |
| Source file names | `agent_<last3>.c`, `controller_<last3>.c`, `Makefile_<last3>` (last 3 digits = `290`) | `agent_290.c`, `controller_290.c`, `Makefile_290` |
| Session ID (SID) tag | last four digits (`2290`) reversed | **`SID:0922`** (kept as a string, leading zero preserved) |
| Authentication token | `OPS-` + last four digits | **`OPS-2290`** |
| Log file | `remoteops_<regno>.log` | `remoteops_IT24102290.log` |
| File storage path | `./agentfiles/<regno>/<filename>` | `./agentfiles/IT24102290/<filename>` |
| Submission archive | `IE3090_<regno>.zip` | `IE3090_IT24102290.zip` |

---

## 2. Repository contents

```
.
├── agent_290.c        # Agent (server) source
├── controller_290.c   # Controller (client) source
├── Makefile_290       # Build file
├── README.md          # This file
├── DESIGN_DIARY.md    # Design decisions and obstacles
└── PROMPT_LOG.md      # Record of AI interactions
```

Generated at runtime (not committed): `agent`, `controller` binaries, `remoteops_IT24102290.log`, and the `agentfiles/IT24102290/` storage directory.

---

## 3. Build instructions

Requirements: `gcc`, `make`, and a standard Linux environment with pthreads.

```bash
# Build both programs
make -f Makefile_290

# Or build individually
make -f Makefile_290 agent
make -f Makefile_290 controller

# Remove binaries
make -f Makefile_290 clean
```

Compiler flags: `-Wall -Wextra -pthread`. The code compiles cleanly with no warnings.

---

## 4. Running

**Start the Agent** (run from the project folder, since the log file and `agentfiles/` are created relative to the working directory):

```bash
./agent
# RemoteOps Agent (IT24102290) listening on port 9410
```

**Connect with the Controller:**

```bash
./controller <agent_ip> 9410
```

**Quick manual test using netcat** (no Controller needed):

```bash
(printf 'AUTH OPS-2290\nQUIT\n'; sleep 1) | nc localhost 9410
# OK AUTHENTICATED SID:0922
# OK BYE SID:0922
```

Verify the Agent is listening on the personalised port:

```bash
ss -tlnp | grep 9410
```

---

## 5. Architecture

### Components
- **Agent (server):** listens on TCP port 9410, accepts multiple simultaneous Controller connections, authenticates each, executes commands, stores/serves files, and streams UDP monitoring data.
- **Controller (client):** connects to the Agent, sends protocol commands, displays responses, and receives the UDP monitoring stream.

### Concurrency model: thread-per-client (`pthread`)
Each accepted connection is handled by its own detached POSIX thread.

**Why:** simple, scales comfortably beyond the required 5 simultaneous clients, and per-client state (authentication status, receive buffer, monitoring flag) lives naturally in a per-connection `client_t` structure. Shared resources (the log file) are protected by a mutex. `SIGPIPE` is ignored and `send()` uses `MSG_NOSIGNAL`, so writing to a dead client cannot crash the Agent.

### Framing
TCP is a byte stream, so each client has a receive buffer (`rbuf`). `read_line()` extracts exactly one `\n`-terminated line at a time, correctly handling:
- a partial line spread across several `recv()` calls,
- multiple lines arriving in a single `recv()`,
- leftover bytes remaining in the buffer (also used for `PUT` file data that follows the command line).

All responses are built by a single `reply()` helper that appends ` SID:0922\n`, so the SID tag cannot be forgotten on any OK or ERR line.

---

## 6. Protocol summary (as specified in the assignment brief, section 2.3)

Every command and response is a single line ending in `\n`. Every response ends with ` SID:0922`.

| Command (Controller to Agent) | Success response | Error response(s) |
|---|---|---|
| `AUTH <token>` | `OK AUTHENTICATED SID:0922` | `ERR 001 AUTH_FAILED SID:0922` |
| `SYSINFO` | `OK SYSINFO <cpu_load> <mem_used_mb> <uptime_sec> SID:0922` | n/a |
| `LISTPROC` | `OK PROCS <comma-separated list> SID:0922` | n/a |
| `EXEC <name>` | `OK EXEC_RESULT <output> SID:0922` | `ERR 002 COMMAND_NOT_ALLOWED SID:0922` |
| `PUT <filename> <filesize>` + raw bytes | `OK FILE_RECEIVED <filename> SID:0922` | `ERR 004 FILE_TOO_LARGE SID:0922` |
| `GET <filename>` | `OK FILE_SEND <filename> <filesize> SID:0922` + raw bytes | `ERR 005 FILE_NOT_FOUND SID:0922` |
| `MONITOR START <udp_port>` | `OK MONITOR_STARTED SID:0922` | n/a |
| `MONITOR STOP` | `OK MONITOR_STOPPED SID:0922` | n/a |
| `QUIT` | `OK BYE SID:0922` | n/a |

**EXEC whitelist (fixed):** `DATE`, `UPTIME`, `DISKFREE`, `HOSTNAME`, `WHOAMI`. Anything else is rejected. The whitelist is never extended to arbitrary shell commands.

**Additional error codes chosen by this implementation** (the brief allows own numeric codes):

| Code | Reason | Meaning |
|---|---|---|
| 003 | `NOT_AUTHENTICATED` | Command sent before a successful `AUTH` |
| 006 | `UNKNOWN_COMMAND` | Command not part of the protocol |
| 007 | `EMPTY_COMMAND` | Blank line received |
| 008 | `LINE_TOO_LONG` | Line exceeded the maximum length |

**Security behaviours:** `AUTH` must succeed before any other command; the connection is dropped after 3 failed attempts; the token itself is never written to the log.

**UDP monitoring:** after `MONITOR START <udp_port>`, the Agent sends datagrams to the Controller's IP on that port in the form `SYSINFO <cpu_load> <mem_used_mb> <uptime_sec> SID:0922` at a fixed interval (every 2 seconds).

---

## 7. Logging

All connections, disconnections, commands, authentication results, and file transfers are written with timestamps to `remoteops_IT24102290.log`, using a mutex so concurrent threads do not interleave entries.

Example:

```
[2026-10-03 14:02:11] AGENT STARTED port=9410
[2026-10-03 14:02:15] CONNECT 127.0.0.1:51234
[2026-10-03 14:02:15] CMD AUTH from 127.0.0.1
[2026-10-03 14:02:15] AUTH OK 127.0.0.1
[2026-10-03 14:02:16] DISCONNECT 127.0.0.1:51234
```

(Replace with a real excerpt from your own log before submission.)

---

## 8. Implementation status

Tick these off as each step is completed and committed.

- [x] Personalised constants (port, SID, token, log file, storage path)
- [x] Listening socket, thread-per-client concurrency, thread-safe logging
- [x] Line framing (`read_line`) and `reply()` with SID tag
- [x] `AUTH` and authentication gate
- [x] `SYSINFO`
- [x] `LISTPROC`
- [x] `EXEC` (whitelist)
- [x] `PUT` (byte-exact upload, `ERR 004` size limit)
- [x] `GET` (byte-exact download, `ERR 005`)
- [x] `MONITOR START` / `MONITOR STOP` (UDP stream)
- [x] `QUIT` stops any active monitoring stream
- [x] Controller program
- [x] Optional extension: transfer throughput (bytes/second) reported by the Controller for PUT and GET

---

## 9. Testing

| # | Test | Expected result | Actual result |
|---|---|---|---|
| 1 | `SYSINFO` before `AUTH` | `ERR 003 NOT_AUTHENTICATED SID:0922` | |
| 2 | `AUTH wrong` | `ERR 001 AUTH_FAILED SID:0922` | |
| 3 | `AUTH OPS-2290` split across two sends (`AU` then `TH OPS-2290\n`) | `OK AUTHENTICATED SID:0922` | |
| 4 | `AUTH`, unknown command, `QUIT` in one send | OK, `ERR 006`, `OK BYE SID:0922` | |
| 5 | 5 simultaneous clients | All served independently | |
| 6 | Client killed abruptly | Agent keeps running, logs disconnect | |

(Extend this table as further features are implemented, and fill in the actual results from your own runs.)

---

## 10. AI usage

AI assistance (Claude) was used for parts of Part 1, in line with the assignment's CLEAR Level 3 policy. All AI interactions are recorded in `PROMPT_LOG.md`, and every code segment was tested and reviewed before inclusion.

---

## 11. Notes and assumptions

- The SID is stored as a string so the leading zero in `0922` is preserved.
- Maximum accepted upload size: 10 MB (`MAX_FILE_SIZE`); larger uploads return `ERR 004 FILE_TOO_LARGE`.
- Developed on CentOS, accessed over SSH, and intended to compile unchanged on the department lab machines.
