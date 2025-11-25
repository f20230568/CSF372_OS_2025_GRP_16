# LLD — Bonus Features (Persistence & UNDO)

## Purpose

Small Low-Level Design for the **ipc_bonus** additions: persistent storage (recovery.log) and undo capability (history stack). This doc focuses only on the bonus features and how they integrate with the existing server/client architecture.

---

## Components Overview

* **Server (process)**

  * Maintains in-memory document (`doc[10][10]`) and per-cell lock owners (`lock_owner[10][10]`).
  * Handles requests from clients via POSIX message queue `MQ_NAME_SERVER`.
  * Persists every successful write (and undo restore) to `recovery.log`.
  * Maintains a LIFO **History Stack** for undo operations.
  * On shutdown writes `output.txt` representing final document snapshot.

* **Client (process)**

  * Sends READ / WRITE / UNDO / SHUTDOWN requests to server.
  * Runs `print_doc` thread performing periodic special READs (is_print flag).
  * Waits for responses from per-client MQ `MQ_NAME_CLIENT_<id>`.

---

## Data Structures

* `char doc[10][10][65]` -- document words (nul-terminated). Max length 64.
* `int lock_owner[10][10]` -- 0 = unlocked, otherwise owner id+1.
* `HistoryNode { int line; int pos; char prev_word[65]; HistoryNode *next; }` -- stack head `history_head`.
* `request_t` / `response_t` structures used on MQ (fixed-size to simplify mq_send/mq_receive).

---

## Persistence (recovery.log)

* **Format**: each persisted record is a single line: `<line> <pos> <word>\n`.
* **When to append**: on every **successful WRITE** and on every **successful UNDO restore**.
* **Append-only**: server appends (O_APPEND) so writes are atomic in most OS setups.
* **Startup recovery**: server reads `recovery.log` from start; for each record it sets `doc[line][pos] = word` in order. This replays writes and restores final state.
* **Silence policy**: recovery runs without printing extra startup logs (to avoid breaking test expectations). Only minimal required logs are emitted.

---

## UNDO (History Stack)

* **Push point**: when a WRITE succeeds (before updating `doc`), push the previous value (which may be empty) onto `history_head`.
* **Undo command**: client sends `REQ_UNDO`.

  * Pop `history_head`.
  * If the popped cell is currently unlocked, restore `doc[line][pos] = prev_word`, append to `recovery.log`, and reply `RESP_SUCCESS`.
  * If the cell is locked, reply `RESP_DROPPED` and **do not** re-push — undo is considered dropped.
  * If history empty, reply `RESP_ERROR`.
* **Atomicity**: UNDO operation is performed under the `doc_mutex` to prevent races with concurrent writes.

---

## Concurrency and Synchronization

* **doc_mutex**: single pthread mutex to protect `doc[][]`, `lock_owner[][]`, and `history_head` operations.
* **Per-cell locking (conceptual)**: `lock_owner[line][pos]` indicates whether a cell is being edited. The server enforces access rules while holding `doc_mutex`.
* **Write duration**: when a write is accepted, server sets the lock owner and spawns a detached `lock_releaser` thread which sleeps for the requested ms and then clears the lock under `doc_mutex`, logging UNLOCK.
* **Message queue serialization**: server processes requests sequentially as they are dequeued; additional protection from `doc_mutex` ensures data integrity when multiple threads (lock releasers) modify lock state.

---

## Protocol (Message Queue payloads)

* **request_t** fields (fixed-size struct): `req_type, client_id, line, pos, word[65], duration_ms, is_print`.
* **response_t** fields: `status, word[65]` (for READ success).
* **REQ_UNDO** uses `req_type = REQ_UNDO` and `client_id` to indicate requester; other fields ignored.
* **Special READs**: clients mark `is_print = 1` when `print_doc` thread issues READs.

  * Server logs successful print reads like normal reads (option B).
  * Server logs dropped print reads as: `Server: Client <id> PRINT_DOC READ(l,p) DROPPED`.

---


## Failure Modes & Edge Cases

* **Partial writes / crash during write**: server persists only after updating `doc[][]` in memory. Because appends are used, replaying the log reconstructs the last persisted value. This does not attempt transactional atomicity across processes.
* **Huge history**: history is unbounded in memory. For production, consider caps or disk-backed history.
* **Locked UNDO target**: UNDO is dropped (design choice) to avoid complex reordering and locking semantics.
* **Invalid coordinates**: requests with out-of-range coords are ignored.

---
