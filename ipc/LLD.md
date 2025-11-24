## Low-Level Design Document: OS Doc Collaborative Editor

This Low-Level Design (LLD) details the implementation choices and strategy for the **OS Doc** client-server collaborative document editing system, adhering to the specifications in the assignment overview and the provided `client.c` and `server.c` source code.

---

### 1. Inter-Process Communication (IPC) Strategy

The system uses **POSIX Message Queues** for all client-server communication.

* **Server Queue ($MQ\_NAME\_SERVER$):** A well-known, write-only queue (`/osdoc_server_mq`) opened by all clients to send requests. The server opens it as read-only. This is the primary channel for clients to send `request_t` messages to the server.
* **Client Queues ($MQ\_NAME\_CLIENT\_PREFIX<id>$):** Each client creates a unique, read-only queue (`/osdoc_client_<id>`) for the server to send responses. The server opens these queues as write-only to send back `response_t` messages.
* **Rationale:** Message Queues provide a robust, asynchronous, and message-oriented IPC mechanism that simplifies client-server request/response handling compared to shared memory (which requires more complex synchronization for message passing) or pipes (which are typically one-to-one). The use of unique client queues enables the server to respond directly and selectively to the correct client.

---

### 2. Data Structures

#### 2.1. Document Storage (Server)

* The document is a $10 \times 10$ grid of words.
* **Data Structure:** A 3D array of characters on the server:
    $$doc[GRID\_SIZE][GRID\_SIZE][MAX\_STRING\_LEN + 1]$$
* **$GRID\_SIZE$** is defined as $10$.
* **$MAX\_STRING\_LEN$** is $64$.
* **Rationale:** This simple array structure directly maps to the document's $10 \times 10$ matrix structure, allowing direct addressing by $doc[line][pos]$.

#### 2.2. Request/Response Structures

* **Request ($\text{request\_t}$):** Sent from client to server. Includes command details.
    * $req\_type$ (int): `REQ_READ` (1), `REQ_WRITE` (2), or `REQ_SHUTDOWN` (3).
    * $client\_id$ (int): ID of the requesting client.
    * $line, pos$ (int): Word coordinates.
    * $word$ (char array): The word string for a WRITE request.
    * $duration\_ms$ (int): Lock duration for a WRITE request.
    * $is\_print$ (int): Flag to mark special print\_doc READ requests.
* **Response ($\text{response\_t}$):** Sent from server to client. Includes outcome and data.
    * $status$ (int): `RESP\_SUCCESS` (0), `RESP\_DROPPED` (1), or `RESP\_ERROR` (2).
    * $word$ (char array): The document word returned for a successful READ.

---

### 3. Access Control and Synchronization Strategy

The access control ensures that only one client can write to a word at a time, and a word being written blocks all other access (reads and writes).

#### 3.1. Lock Mechanism (Server)

* **Data Structure:** An integer array on the server to track the owner of each word's lock:
    $$lock\_owner[GRID\_SIZE][GRID\_SIZE]$$
* **Lock State:**
    * **0:** No lock, word is available for Read/Write.
    * **Client ID + 1:** Locked by the client with the corresponding ID. (Using ID+1 avoids conflict with the '0' state).
* **Synchronization:** A single **Pthread Mutex** ($\text{doc\_mutex}$) is used to protect the entire document and the $\text{lock\_owner}$ array. This ensures that lock checking, status updating, and word writing operations are atomic, preventing race conditions during access control decisions.

#### 3.2. WRITE Operation Strategy

1.  Client sends `REQ_WRITE` with $(line, pos)$, word, and $duration\_ms$.
2.  Server locks $\text{doc\_mutex}$.
3.  **Access Check:** If $lock\_owner[line][pos] \neq 0$, the request is **DROPPED** (status $\text{RESP\_DROPPED}$).
4.  If available:
    * $lock\_owner[line][pos]$ is set to $client\_id + 1$.
    * $doc[line][pos]$ is updated with the new word.
    * Response $\text{RESP\_SUCCESS}$ is sent back to the client.
5.  Server unlocks $\text{doc\_mutex}$.
6.  **Lock Release (Asynchronous):** A **detached Pthread** ($\text{lock\_releaser}$) is created to handle the timed lock release.
    * This thread sleeps for $duration\_ms$.
    * After sleeping, it locks $\text{doc\_mutex}$, checks if $lock\_owner$ is still owned by the *original client* ($\text{client\_id} + 1$), and resets it to 0. This check is crucial to prevent releasing a lock that a different client might have acquired immediately after the initial client's lock should have expired (though in this design, immediate re-acquisition shouldn't happen without server intervention).
    * The use of a detached thread ensures the main server loop isn't blocked by the write duration.

#### 3.3. READ Operation Strategy

1.  Client sends `REQ_READ` with $(line, pos)$.
2.  Server locks $\text{doc\_mutex}$.
3.  **Access Check:**
    * If $lock\_owner[line][pos] \neq 0$: The word is being edited.
        * If the request is for $\text{print\_doc}$ ($is\_print = 1$): The request is **DROPPED** ($\text{RESP\_DROPPED}$). This implements the "STRICT RULE" to fix the locked words test case.
        * If the request is a normal read ($is\_print = 0$):
            * If $lock\_owner$ is *not* the requesting client: **DROPPED** ($\text{RESP\_DROPPED}$).
            * If $lock\_owner$ *is* the requesting client: **ALLOWED** ($\text{RESP\_SUCCESS}$). This fixes the test case where a client reads a word it is currently editing (though the assignment implies writes block all reads, this relaxed rule is necessary to pass *Test1* as noted in the source code).
    * If $lock\_owner[line][pos] = 0$: The word is available, **ALLOWED** ($\text{RESP\_SUCCESS}$).
4.  If allowed, the word $doc[line][pos]$ is copied into the response.
5.  Server unlocks $\text{doc\_mutex}$.
6.  Response is sent to the client.

---

### 4. Client-Specific Implementations

#### 4.1. Command Processing

* The client reads `input.txt` sequentially, line by line.
* It filters commands based on the `C<client_id>` prefix, only processing commands designated for its ID.
* **Blocking:** For READ and WRITE, the client is **synchronous**; it sends the request via $\text{mq\_send}$ and then waits for the response via $\text{mq\_timedreceive}$ using the local client message queue. A $2$-second timeout is used to prevent indefinite blocking. A $\text{pthread\_mutex}$ ($\text{comm\_mutex}$) is used to ensure only one thread (main or $\text{print\_doc}$) is sending a request and waiting for a response at a time.

#### 4.2. $\text{print\_doc}$ Thread

* Each client spawns a separate, detached thread ($\text{print\_doc\_thread}$) at startup.
* This thread continuously runs a loop that:
    1.  Sleeps for $2$ seconds.
    2.  Iterates through all $10 \times 10$ coordinates $(line, pos)$.
    3.  For each coordinate, it issues a special $\text{REQ\_READ}$ request with the $is\_print$ flag set to 1.
    4.  It waits for the response. If successful, the word is collected. If dropped or error, it writes "???" if $\text{print\_doc}$ logic allows (i.e., if $\text{any}$ word on the line has been found so far).
    5.  After collecting all $100$ words, it writes the document state to `output_client<id>.txt`.
* **Rationale:** Using a separate thread keeps the $\text{print\_doc}$ activity non-blocking to the main client command execution flow, fulfilling the requirement for periodic document printing. The $is\_print$ flag distinguishes these reads for special server-side access control (strict dropping if locked).

---

### 5. System Shutdown

1.  The client finishes processing `input.txt` and sleeps briefly.
2.  The client sets the $\text{running}$ flag to 0, causing the $\text{print\_doc}$ thread to exit its loop and be joined.
3.  The client sends a $\text{REQ\_SHUTDOWN}$ request to the server.
4.  The server tracks active clients using an $\text{active\_clients}$ array. Upon receiving a $\text{REQ\_SHUTDOWN}$, it marks that client as inactive.
5.  When all clients have been seen ($\text{seen\_any\_client} = 1$) and all are inactive, the server breaks its main loop, calls $\text{write\_output\_file()}$ to generate the final `output.txt`, and cleans up its message queue before exiting.