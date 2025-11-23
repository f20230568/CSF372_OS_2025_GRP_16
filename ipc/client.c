/* ----------------------------------------------------------------------------
   CHANGED: THIS client.c WAS MODIFIED/REPLACED BY THE ASSISTANT.
   IMPLEMENTS POSIX MESSAGE-QUEUE BASED CLIENT FOR OS DOC ASSIGNMENT.
   FEATURES:
   - Reads commands from input.txt (filters by client id C<id> ...)
   - Sends READ and WRITE requests to server and waits for reply
   - Print_doc thread issues special READs every 2 seconds and writes
     output_client<id>.txt
   - Uses provided ts_printf wrapper (printf macro)
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

/* client id global for print_doc file naming per the template */
int client_id_global = 0;

/* message queue handles */
mqd_t server_mqd = (mqd_t)-1;
mqd_t client_mqd = (mqd_t)-1;

/* send request to server and block waiting for server's response on client queue */
static int send_request_and_wait_response(request_t *req, response_t *resp) {
    if (mq_send(server_mqd, (const char*)req, sizeof(request_t), 0) == -1) {
        printf("Client %d: Failed to send request to server: %s\n", req->client_id, strerror(errno));
        resp->status = RESP_ERROR;
        return -1;
    }
    ssize_t r = mq_receive(client_mqd, (char*)resp, MAX_MSG_SIZE, NULL);
    if (r == -1) {
        printf("Client %d: mq_receive failed: %s\n", req->client_id, strerror(errno));
        resp->status = RESP_ERROR;
        return -1;
    }
    return 0;
}

/* print_doc thread: every 2 seconds, perform special READs for all positions
   and write output_client<id>.txt. Special reads set is_print=1 which server
   will log differently. */
static void *print_doc_thread(void *arg) {
    int cid = *(int*)arg;
    free(arg);
    request_t req;
    response_t resp;
    char outname[64];
    snprintf(outname, sizeof(outname), "output_client%d.txt", cid);

    while (1) {
        sleep(2);
        FILE *of = fopen(outname, "w");
        if (!of) {
            printf("Client %d: Failed to open %s for writing: %s\n", cid, outname, strerror(errno));
            continue;
        }
        for (int line = 0; line < GRID_SIZE; ++line) {
            int any = 0;
            char linebuf[1024];
            linebuf[0] = '\0';
            for (int pos = 0; pos < GRID_SIZE; ++pos) {
                req.req_type = REQ_READ;
                req.client_id = cid;
                req.line = line;
                req.pos = pos;
                req.is_print = 1;
                req.word[0] = '\0';
                req.duration_ms = 0;

                /* send and wait */
                if (send_request_and_wait_response(&req, &resp) == -1) {
                    /* treat as dropped/error: write ??? */
                    if (any) strncat(linebuf, " ", sizeof(linebuf) - strlen(linebuf) - 1);
                    strncat(linebuf, "???", sizeof(linebuf) - strlen(linebuf) - 1);
                    any = 1;
                    continue;
                }

                if (resp.status == RESP_SUCCESS) {
                    if (resp.word[0] != '\0') {
                        if (any) strncat(linebuf, " ", sizeof(linebuf) - strlen(linebuf) - 1);
                        strncat(linebuf, resp.word, sizeof(linebuf) - strlen(linebuf) - 1);
                        any = 1;
                    }
                } else {
                    /* Dropped (writing ongoing). For print_doc we put ??? per spec. */
                    if (any) strncat(linebuf, " ", sizeof(linebuf) - strlen(linebuf) - 1);
                    strncat(linebuf, "???", sizeof(linebuf) - strlen(linebuf) - 1);
                    any = 1;
                }
            }
            if (any) fprintf(of, "%s\n", linebuf);
        }
        fclose(of);
        printf("Client %d: PRINT_DOC written to %s\n", cid, outname);
    }
    return NULL;
}

/* Helper: sleep ms */
static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <client_id> [input.txt]\n", argv[0]);
        return 1;
    }

    int cid = atoi(argv[1]);
    client_id_global = cid;

    const char *input_filename = "input.txt";
    if (argc >= 3) input_filename = argv[2];

    /* Open server mq for writing */
    server_mqd = mq_open(MQ_NAME_SERVER, O_WRONLY);
    if (server_mqd == (mqd_t)-1) {
        fprintf(stderr, "Client %d: mq_open server failed: %s\n", cid, strerror(errno));
        return 1;
    }

    /* Create client queue for receiving responses */
    char mqname[64];
    snprintf(mqname, sizeof(mqname), "%s%d", MQ_NAME_CLIENT_PREFIX, cid);
    mq_unlink(mqname);
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = MQ_MAXMSG;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    client_mqd = mq_open(mqname, O_CREAT | O_RDONLY, 0666, &attr);
    if (client_mqd == (mqd_t)-1) {
        fprintf(stderr, "Client %d: mq_open client failed: %s\n", cid, strerror(errno));
        mq_close(server_mqd);
        return 1;
    }

    printf("Client %d: Starting\n", cid);

    /* Start print_doc thread */
    pthread_t ptid;
    int *parg = malloc(sizeof(int));
    *parg = cid;
    pthread_create(&ptid, NULL, print_doc_thread, parg);
    pthread_detach(ptid);

    /* Open input file and process commands in order */
    FILE *f = fopen(input_filename, "r");
    if (!f) {
        printf("Client %d: Failed to open %s: %s\n", cid, input_filename, strerror(errno));
        /* keep running so print_doc continues (runner may later kill) */
        while (1) sleep(1);
        return 1;
    }

    char linebuf[512];
    while (fgets(linebuf, sizeof(linebuf), f)) {
        /* skip leading whitespace */
        char *p = linebuf;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '\r') continue;
        /* expect lines like: C<id> COMMAND ... */
        if (*p != 'C') continue;
        int target_id = -1;
        int consumed = 0;
        if (sscanf(p, "C%d %n", &target_id, &consumed) < 1) continue;
        p += consumed;
        if (target_id != cid) continue; /* not for this client */

        /* parse command */
        char cmd[32];
        consumed = 0;
        if (sscanf(p, " %31s %n", cmd, &consumed) < 1) continue;
        p += consumed;

        if (strcmp(cmd, "SLEEP") == 0) {
            int tm = 0;
            if (sscanf(p, " %d", &tm) == 1) {
                printf("Client %d: SLEEP %dms\n", cid, tm);
                msleep(tm);
            }
        } else if (strcmp(cmd, "READ") == 0) {
            int l, pos;
            if (sscanf(p, " %d %d", &l, &pos) == 2) {
                request_t req;
                response_t resp;
                req.req_type = REQ_READ;
                req.client_id = cid;
                req.line = l;
                req.pos = pos;
                req.is_print = 0;
                req.word[0] = '\0';
                req.duration_ms = 0;

                printf("Client %d: Requesting READ(%d,%d)\n", cid, l, pos);
                if (send_request_and_wait_response(&req, &resp) == -1) {
                    printf("Client %d: READ(%d,%d) ERROR\n", cid, l, pos);
                } else {
                    if (resp.status == RESP_SUCCESS) {
                        printf("Client %d: READ(%d,%d) SUCCESS - Value: '%s'\n", cid, l, pos, resp.word);
                    } else if (resp.status == RESP_DROPPED) {
                        printf("Client %d: READ(%d,%d) DROPPED\n", cid, l, pos);
                    } else {
                        printf("Client %d: READ(%d,%d) ERROR\n", cid, l, pos);
                    }
                }
            }
        } else if (strcmp(cmd, "WRITE") == 0) {
            int l, pos, tm;
            char word[MAX_STRING_LEN + 1];
            if (sscanf(p, " %d %d %63s %d", &l, &pos, word, &tm) == 4) {
                request_t req;
                response_t resp;
                req.req_type = REQ_WRITE;
                req.client_id = cid;
                req.line = l;
                req.pos = pos;
                strncpy(req.word, word, MAX_STRING_LEN);
                req.word[MAX_STRING_LEN] = '\0';
                req.duration_ms = tm;
                req.is_print = 0;

                printf("Client %d: Requesting WRITE lock for (%d,%d)\n", cid, l, pos);
                if (send_request_and_wait_response(&req, &resp) == -1) {
                    printf("Client %d: WRITE(%d,%d) ERROR\n", cid, l, pos);
                } else {
                    if (resp.status == RESP_SUCCESS) {
                        printf("Client %d: WRITE(%d,%d) = '%s', sleeping for %dms\n", cid, l, pos, req.word, tm);
                        /* client does not sleep to hold lock; server enforces lock duration */
                    } else if (resp.status == RESP_DROPPED) {
                        printf("Client %d: WRITE(%d,%d) DROPPED\n", cid, l, pos);
                    } else {
                        printf("Client %d: WRITE(%d,%d) ERROR\n", cid, l, pos);
                    }
                }
            }
        } else {
            /* unknown command - ignore */
        }
    }

    fclose(f);

    /* After processing commands, keep running so print_doc continues until runner ends process */
    while (1) {
        sleep(1);
    }

    /* cleanup (unreachable in runner-managed tests) */
    mq_close(client_mqd);
    mq_unlink(mqname);
    mq_close(server_mqd);

    return 0;
}
