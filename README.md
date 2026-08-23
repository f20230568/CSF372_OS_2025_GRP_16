# Operating Systems Laboratory Projects

This repository contains implementations of core Operating System concepts, ranging from command-line interpreters and IPC-driven collaborative tools to kernel-level virtual memory and demand paging mechanisms in C.

---

## Branch Overview

### `shell`

* **Custom Unix Shell:** Implements a user-space command-line interface capable of executing standard system binaries and built-in commands.
* **Process Management & Concurrency:** Handles process creation, execution, and termination using `fork()`, `execvp()`, and `waitpid()`.
* **I/O & Pipeline Control:** Supports standard input/output redirection (`<`, `>`, `>>`) and multi-stage command pipelining (`|`).
* **Signal Handling:** Configured custom signal handlers for interrupt and background task management.

---

### `ipc`

* **Inter-Process Communication:** Implements multi-process synchronization and data exchange using IPC mechanisms (pipes, shared memory, and message queues).
* **Synchronization Primitives:** Utilizes mutexes and semaphores to prevent race conditions and enforce critical section safety across concurrent processes.
* **Client-Server Architecture:** Facilitates coordinated multi-client communication and state management.

---

### `ipc_bonus`

* **Extended IPC Functionality:** Inherits the core inter-process communication and synchronization design from the `ipc` branch.
* **Stack-Based State Recovery:** Implements dynamic **Undo** and **Redo** operations using dedicated stack data structures to track and revert collaborative state changes across active processes.

---

### `demand_paging_assignment`

* **Virtual Memory Management:** Emulates OS kernel paging mechanisms, address translation, and dynamic physical memory allocation.
* **Page Fault Handling:** Implements page-fault resolution routines to allocate frames and load missing pages on demand.
* **Page Replacement Policies:** Integrates LRU page eviction algorithm to manage frame allocation under high memory pressure.
* **Memory Protection & Statistics:** Enforces memory bounds and tracks page hits, page faults, and disk I/O overhead.

---

## Key Highlights

* **Thread & Process Concurrency:** Engineered priority scheduling, thread safety, and race-free concurrent execution.
* **System Call Interface:** Constructed robust user-space to kernel-space interactions and parameter validation.
* **Memory Virtualization:** Optimized memory footprint via lazy loading, frame tables, and replacement algorithms.
