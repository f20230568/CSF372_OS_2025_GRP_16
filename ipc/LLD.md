# Low-Level Design (LLD) — OS Doc (Small)

**Scope**: concise implementation choices and strategy for `server.c` and `client.c` (basic assignment). Bonus not implemented.

---

## Summary

Implement a 10x10 collaborative document where multiple clients concurrently READ/WRITE words. Words are independently controlled so we maximize concurrency. The design below focuses on correctness, simplicity, and testability with the provided `runner.py`.

---

## High-level choices

* **IPC mechanism**: POSIX message queues (or System V message queues as an alternative).

  * Rationale: message queues provide a simple request/response communication pattern without sockets. They are already decoupled (server and clients don’t need shared memory bootstrap), and the runner can spawn multiple processes cleanly.
* **Per-word locking**: per-word state tracked on server (100 entries). Each word has an owner (editor) and an expiration time for the held edit.

  * Rationale: per-word locking gives maximal concurrency (different words can be edited/read independently).
* **Synchronization inside server**: `pthread_mutex_t` to protect shared server datastructures and `pthread_cond_t` not required because operations are instantaneous or dropped (no waiting queues are required by spec).
* **Client-server protocol**: simple typed messages sent over message queues containing: client id, request type (READ, WRITE, PRINT_READ), coordinates, for WRITE: word and edit-duration. Server replies with a response message (OK / DROPPED + value for READs).

---

## Server internal design

### Data structures

```c
// word slot
struct WordSlot {
  char value[65];        // current word ("" == empty)
  int editing_client;    // -1 if free, else client id holding exclusive edit
  uint64_t expire_ts;    // monotonic ms when editing ends; 0 if not editing
};

static struct WordSlot grid[10][10];
static pthread_mutex_t grid_lock; // guard grid updates & checks
```

### Message handling

* Server creates a *well-known* server queue name (fixed) for incoming requests.
* Each client creates a private response queue with a name derived from its id. Client includes its response queue name in each request.
* Server runs a message loop: receive request → lock `grid_lock` → evaluate → modify state if WRITE granted → unlock → send response.

### Request semantics

* **READ**:

  * If `editing_client == -1` or `expire_ts` has passed, succeed and return current word.
  * If currently editing (expire_ts > now and editing_client != requester), return DROPPED.
* **WRITE**:

  * If free (same rules) → update `value` immediately, set `editing_client` and `expire_ts = now + duration_ms`, respond OK.
  * Else respond DROPPED.
* **PRINT_DOC READS** (special): behave like READ but logged differently; server sends `PRINT_DOC READ(x,y) DROPPED` if dropped.

### Editing expiry handling

* Server does NOT spawn a timer per edit. Instead, when processing any incoming request (or periodic maintenance thread), server compares `expire_ts` to current time and clears expired `editing_client` if needed. This avoids many active timers and keeps code simple. A lightweight maintenance thread (optional) can run every 10-50ms to clear expirations if desired.

### Logging

* All output uses `ts_printf()` to match runner expectations (server logs LOCK GRANTED / DROPPED etc.).

### Shutdown & output

* On SIGINT or when runner finishes, server writes `output.txt` with `print_doc()` semantics: iterate rows, print non-empty lines with words separated by spaces.
* Cleanup: remove message queues.

---

## Client design

* Each client reads the common `input.txt`, filters commands for its `C<id>`.
* Creates a response MQ and sends requests to server MQ. For each request, waits for a response synchronously (blocking read on its response MQ).
* `SLEEP` commands: use `usleep()` with ms resolution.
* **Print thread**: background pthread that wakes every 2000ms and issues a sequence of special PRINT_READ requests for all 100 cells (or line-by-line) to build current doc; writes file `output_client<id>.txt`. Each special read is subject to normal dropping rules and is logged specially by server.
* All client logs use `ts_printf()`.

---

## Concurrency & Correctness notes

* Because server holds `grid_lock` briefly while checking and updating a single word, operations are atomic and consistent.
* Multiple readers are allowed because READ does not change state if no editor exists. Implementation still serializes access via `grid_lock` for correctness but returns immediately, so contention is minimal.
* Writes are exclusive via `editing_client` field.
* No waiting queues: per spec, attempts during edit are DROPPED instead of queued.

---

## Error handling & robustness

* Validate coordinates (0..9) and word length on server; return error to client if invalid.
* Defensive checks for MQ creation/permissions; log and exit cleanly.
* Use `clock_gettime(CLOCK_MONOTONIC)` for expire logic to avoid time-of-day jumps.

---



*End of small LLD.*
