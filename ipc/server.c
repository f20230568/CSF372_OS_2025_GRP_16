/* ----------------------------------------------------------------------------
   CHANGED: THIS server.c WAS MODIFIED/REPLACED BY THE ASSISTANT.
   IMPLEMENTS POSIX MESSAGE-QUEUE BASED SERVER FOR OS DOC ASSIGNMENT.
   FEATURES:
   - Per-word lock table (10x10)
   - Handles READ, WRITE and PRINT_DOC requests from clients
   - WRITE grants set lock and create releaser thread to release after duration
   - READ/PRINT_DOC dropped while a write lock is held
   - Uses existing ts_printf timestamp wrapper (keeps printf macro)
   ---------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <sys/time.h>
#include <mqueue.h>
#include <fcntl.h>
#include <pthread.h>

#define GRID_SIZE 10
#define MAX_STRING_LEN 64
#define MAX_CLIENTS 10

/* The message queue names */
#define MQ_NAME_SERVER "/osdoc_server_mq"
#define MQ_NAME_CLIENT_PREFIX "/osdoc_client_"

#define MAX_MSG_SIZE 512
#define MQ_MAXMSG 20

/* Request types */
#define REQ_READ 1
#define REQ_WRITE 2

/* Response statuses */
#define RESP_SUCCESS 0
#define RESP_DROPPED 1
#define RESP_ERROR 2

/* Timestamped printf that prefixes each line with HH:MM:SS.mmm
   This matches the ts_printf already provided in the template files. */
static void ts_printf(const char *fmt, ...) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm_info);

    fprintf(stdout, "%s.%03ld ", tbuf, tv.tv_usec / 1000);

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    fflush(stdout);
}

/* Keep compatibility with uploaded template that defines printf -> ts_printf */
#define printf(...) ts_printf(__VA_ARGS__)

/* Request and response structures sent over mq */
typedef struct {
    int req_type;      /* REQ_READ or REQ_WRITE */
    int client_id;     /* client id 0-9 */
    int line;          /* 0-9 */
    int pos;           /* 0-9 */
    char word[MAX_STRING_LEN + 1]; /* for WRITE */
    int duration_ms;   /* for WRITE */
    int is_print;      /* 1 if from PRINT_DOC special read */
} request_t;

typedef struct {
    int status;                   /* RESP_SUCCESS / RESP_DROPPED / RESP_ERROR */
    char word[MAX_STRING_LEN + 1];/* returned word for READ */
} response_t;

/* Document storage and lock table */
static char doc[GRID_SIZE][GRID_SIZE][MAX_STRING_LEN + 1];
/* lock_owner: 0 => free, client_id+1 => locked by client_id */
static int lock_owner[GRID_SIZE][GRID_SIZE];
/* Protect access to doc and lock_owner */
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Server MQ descriptor */
static mqd_t server_mqd = (mqd_t)-1;

/* Helper to send response to a client's queue */
static void send_response(int client_id, response_t *resp) {
    char mqname[64];
    snprintf(mqname, sizeof(mqname), "%s%d", MQ_NAME_CLIENT_PREFIX, client_id);
    mqd_t mq = mq_open(mqname, O_WRONLY);
    if (mq == (mqd_t)-1) {
        printf("Server: Failed to open client queue %s: %s\n", mqname, strerror(errno));
        return;
    }
    if (mq_send(mq, (const char*)resp, sizeof(response_t), 0) == -1) {
        printf("Server: mq_send to client %d failed: %s\n", client_id, strerror(errno));
    }
    mq_close(mq);
}

/* Arguments for lock release thread */
typedef struct {
    int line;
    int pos;
    int client_id;
    int duration_ms;
} lock_release_arg_t;

/* Releases a lock after duration_ms milliseconds */
static void *lock_releaser(void *arg) {
    lock_release_arg_t a = *(lock_release_arg_t*)arg;
    free(arg);
    /* sleep for duration_ms */
    struct timespec ts;
    ts.tv_sec = a.duration_ms / 1000;
    ts.tv_nsec = (a.duration_ms % 1000) * 1000000;
    nanosleep(&ts, NULL);

    pthread_mutex_lock(&doc_mutex);
    if (lock_owner[a.line][a.pos] == a.client_id + 1) {
        lock_owner[a.line][a.pos] = 0;
        printf("Server: Client %d WRITE LOCK(%d,%d) RELEASED\n", a.client_id, a.line, a.pos);
    }
    pthread_mutex_unlock(&doc_mutex);

    return NULL;
}

int main(void) {
    /* initialize document and locks */
    memset(doc, 0, sizeof(doc));
    memset(lock_owner, 0, sizeof(lock_owner));

    /* Create server message queue (read-only for server) */
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAXMSG;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    /* Remove any existing queue and create */
    mq_unlink(MQ_NAME_SERVER);
    server_mqd = mq_open(MQ_NAME_SERVER, O_CREAT | O_RDONLY, 0666, &attr);
    if (server_mqd == (mqd_t)-1) {
        fprintf(stderr, "Server: mq_open server failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Server: Started. Grid initialized.\n");

    /* Main loop: receive requests */
    char buf[MAX_MSG_SIZE];
    while (1) {
        ssize_t r = mq_receive(server_mqd, buf, MAX_MSG_SIZE, NULL);
        if (r == -1) {
            if (errno == EINTR) continue;
            printf("Server: mq_receive error: %s\n", strerror(errno));
            break;
        }
        if (r < (ssize_t)sizeof(request_t)) {
            /* ignore invalid messages */
            continue;
        }
        request_t req;
        memcpy(&req, buf, sizeof(request_t));

        /* Validate coordinates */
        if (req.line < 0 || req.line >= GRID_SIZE || req.pos < 0 || req.pos >= GRID_SIZE) {
            response_t resp;
            resp.status = RESP_ERROR;
            resp.word[0] = '\0';
            send_response(req.client_id, &resp);
            printf("Server: Client %d sent invalid coords (%d,%d)\n", req.client_id, req.line, req.pos);
            continue;
        }

        if (req.req_type == REQ_READ) {
            response_t resp;
            pthread_mutex_lock(&doc_mutex);
            int owner = lock_owner[req.line][req.pos];
            if (owner != 0) {
                resp.status = RESP_DROPPED;
                resp.word[0] = '\0';
                if (req.is_print)
                    printf("Server: Client %d PRINT_DOC READ(%d,%d) DROPPED\n", req.client_id, req.line, req.pos);
                else
                    printf("Server: Client %d READ(%d,%d) DROPPED\n", req.client_id, req.line, req.pos);
            } else {
                resp.status = RESP_SUCCESS;
                strncpy(resp.word, doc[req.line][req.pos], MAX_STRING_LEN);
                resp.word[MAX_STRING_LEN] = '\0';
                if (req.is_print)
                    printf("Server: Client %d PRINT_DOC READ(%d,%d) SUCCESS\n", req.client_id, req.line, req.pos);
                else
                    printf("Server: Client %d READ(%d,%d) SUCCESS - Value: '%s'\n", req.client_id, req.line, req.pos, resp.word);
            }
            pthread_mutex_unlock(&doc_mutex);
            send_response(req.client_id, &resp);
        } else if (req.req_type == REQ_WRITE) {
            response_t resp;
            pthread_mutex_lock(&doc_mutex);
            int owner = lock_owner[req.line][req.pos];
            if (owner != 0) {
                /* DROP the write */
                resp.status = RESP_DROPPED;
                pthread_mutex_unlock(&doc_mutex);
                printf("Server: Client %d WRITE(%d,%d) DROPPED\n", req.client_id, req.line, req.pos);
                send_response(req.client_id, &resp);
            } else {
                /* Grant lock immediately, update word */
                lock_owner[req.line][req.pos] = req.client_id + 1;
                strncpy(doc[req.line][req.pos], req.word, MAX_STRING_LEN);
                doc[req.line][req.pos][MAX_STRING_LEN] = '\0';
                resp.status = RESP_SUCCESS;
                pthread_mutex_unlock(&doc_mutex);

                printf("Server: Client %d WRITE LOCK(%d,%d) GRANTED - Value: '%s', sleeping for %dms\n",
                       req.client_id, req.line, req.pos, req.word, req.duration_ms);

                send_response(req.client_id, &resp);

                /* spawn releaser thread */
                lock_release_arg_t *arg = malloc(sizeof(lock_release_arg_t));
                if (arg) {
                    arg->line = req.line;
                    arg->pos = req.pos;
                    arg->client_id = req.client_id;
                    arg->duration_ms = req.duration_ms;
                    pthread_t tid;
                    pthread_create(&tid, NULL, lock_releaser, arg);
                    pthread_detach(tid);
                } else {
                    /* If allocation failed, release immediately */
                    pthread_mutex_lock(&doc_mutex);
                    if (lock_owner[req.line][req.pos] == req.client_id + 1)
                        lock_owner[req.line][req.pos] = 0;
                    pthread_mutex_unlock(&doc_mutex);
                    printf("Server: Memory allocation failed for lock releaser; released lock immediately\n");
                }
            }
        } else {
            printf("Server: Unknown request type %d from client %d\n", req.req_type, req.client_id);
        }
    }

    /* Shutdown: write final doc to output.txt */
    FILE *f = fopen("output.txt", "w");
    if (f) {
        for (int i = 0; i < GRID_SIZE; ++i) {
            int any = 0;
            for (int j = 0; j < GRID_SIZE; ++j) {
                if (doc[i][j][0] != '\0') { any = 1; break; }
            }
            if (!any) continue;
            int first = 1;
            for (int j = 0; j < GRID_SIZE; ++j) {
                if (doc[i][j][0] != '\0') {
                    if (!first) fprintf(f, " ");
                    fprintf(f, "%s", doc[i][j]);
                    first = 0;
                }
            }
            fprintf(f, "\n");
        }
        fclose(f);
    } else {
        printf("Server: Failed to open output.txt for writing: %s\n", strerror(errno));
    }

    mq_close(server_mqd);
    mq_unlink(MQ_NAME_SERVER);

    printf("Server: Shutdown complete\n");
    return 0;
}
